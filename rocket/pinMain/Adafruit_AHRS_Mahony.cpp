#include "Adafruit_AHRS_Mahony.h"
#include <math.h>
#include <Arduino.h>
//-------------------------------------------------------------------------------------------

#define DEFAULT_SAMPLE_FREQ 200.0f // sample frequency in Hz
#define twoKpDef (2.0f * 1.5f)     // 2 * proportional gain
#define twoKiDef (2.0f * 0.2f)     // 2 * integral gain
//-------------------------------------------------------------------------------------------
//업데이트

Adafruit_Mahony::Adafruit_Mahony() : Adafruit_Mahony(twoKpDef, twoKiDef) {}

Adafruit_Mahony::Adafruit_Mahony(float prop_gain, float int_gain) {
  twoKp = prop_gain; // 2 * proportional gain (Kp)
  twoKi = int_gain;  // 2 * integral gain (Ki)
  q0 = 1.0f;
  q1 = 0.0f;
  q2 = 0.0f;
  q3 = 0.0f;
  integralFBx = 0.0f;
  integralFBy = 0.0f;
  integralFBz = 0.0f;
  anglesComputed = false;
  invSampleFreq = 1.0f / DEFAULT_SAMPLE_FREQ;
}

void Adafruit_Mahony::update(float gx, float gy, float gz, float ax, float ay,
                             float az, float mx, float my, float mz, float dt) {
  float recipNorm;
  float q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;
  float hx, hy, bx, bz;
  float halfvx, halfvy, halfvz, halfwx, halfwy, halfwz;
  float halfex, halfey, halfez;
  float qa, qb, qc;

  // 단위 변환
  //gx *= 0.0174533f;
  //gy *= 0.0174533f;
  //gz *= 0.0174533f;

  // 가속도계 측정값이 유효할때
  if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

    // 가속도 정규화 => 크기 1로만들어서 방향만 사용 
    recipNorm = invSqrt(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    // 마그네토미터 정규화 => 크기 1로만들어서 방향만 사용 
    recipNorm = invSqrt(mx * mx + my * my + mz * mz);
    mx *= recipNorm;
    my *= recipNorm;
    mz *= recipNorm;

    // 쿼터니안 곱 미리계산(메모리절약)
    q0q0 = q0 * q0;
    q0q1 = q0 * q1;
    q0q2 = q0 * q2;
    q0q3 = q0 * q3;
    q1q1 = q1 * q1;
    q1q2 = q1 * q2;
    q1q3 = q1 * q3;
    q2q2 = q2 * q2;
    q2q3 = q2 * q3;
    q3q3 = q3 * q3;


    //회전행렬
    //수평자기장성분(hx, hy)
    hx = 2.0f *
         (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2));
    hy = 2.0f *
         (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) + mz * (q2q3 - q0q1));
    //수평자기장의 크기(bx)
    bx = sqrtf(hx * hx + hy * hy);
    //수직자기장의 성분(hz)
    bz = 2.0f *
         (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5f - q1q1 - q2q2));

    // 중력,자기장 방향 추정
    halfvx = q1q3 - q0q2;
    halfvy = q0q1 + q2q3;
    halfvz = q0q0 - 0.5f + q3q3;

    halfwx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
    halfwy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);
    halfwz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);

    //오차(측정방향과 추정방향의 오차)
    halfex = (ay * halfvz - az * halfvy) + (my * halfwz - mz * halfwy);
    halfey = (az * halfvx - ax * halfvz) + (mz * halfwx - mx * halfwz);
    halfez = (ax * halfvy - ay * halfvx) + (mx * halfwy - my * halfwx);

    // PI제어의 I항 오차의 누적을 통한 자이로bias 보정
    if (twoKi > 0.0f) {
      // 오차적분
      integralFBx += twoKi * halfex * dt;
      integralFBy += twoKi * halfey * dt;
      integralFBz += twoKi * halfez * dt;
      // 보정된 자이로값
      gx += integralFBx;  
      gy += integralFBy;
      gz += integralFBz;
    } else {
      integralFBx = 0.0f; //ki가 0일때 
      integralFBy = 0.0f;
      integralFBz = 0.0f;
    }

    // PI제어의 P항 
    gx += twoKp * halfex;
    gy += twoKp * halfey;
    gz += twoKp * halfez;
  }

  //쿼터니안(센서가 지상좌표계에 얼마나 기울었는지)
  gx *= (0.5f * dt); 
  gy *= (0.5f * dt);
  gz *= (0.5f * dt);
  qa = q0;
  qb = q1;
  qc = q2;
  q0 += (-qb * gx - qc * gy - q3 * gz);
  q1 += (qa * gx + qc * gz - q3 * gy);
  q2 += (qa * gy - qb * gz + q3 * gx);
  q3 += (qa * gz + qb * gy - qc * gx);

  // 쿼터니안 정규화
  recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm;
  q1 *= recipNorm;
  q2 *= recipNorm;
  q3 *= recipNorm;
  anglesComputed = 0;
}

//-------------------------------------------------------------------------------------------
//마그네토미터 측정 없이
void Adafruit_Mahony::updateIMU(float gx, float gy, float gz, float ax,
                                float ay, float az, float dt) {
  float recipNorm;
  float halfvx, halfvy, halfvz;
  float halfex, halfey, halfez;
  float qa, qb, qc;

  //gx *= 0.0174533f;
  //gy *= 0.0174533f;
  //gz *= 0.0174533f;


  if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

   
    recipNorm = invSqrt(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

 
    halfvx = q1 * q3 - q0 * q2;
    halfvy = q0 * q1 + q2 * q3;
    halfvz = q0 * q0 - 0.5f + q3 * q3;

    halfex = (ay * halfvz - az * halfvy);
    halfey = (az * halfvx - ax * halfvz);
    halfez = (ax * halfvy - ay * halfvx);

    if (twoKi > 0.0f) {
      integralFBx += twoKi * halfex * dt;
      integralFBy += twoKi * halfey * dt;
      integralFBz += twoKi * halfez * dt;
      gx += integralFBx; 
      gy += integralFBy;
      gz += integralFBz;
    } else {
      integralFBx = 0.0f; 
      integralFBy = 0.0f;
      integralFBz = 0.0f;
    }

    gx += twoKp * halfex;
    gy += twoKp * halfey;
    gz += twoKp * halfez;
  }

  gx *= (0.5f * dt); 
  gy *= (0.5f * dt);
  gz *= (0.5f * dt);
  qa = q0;
  qb = q1;
  qc = q2;
  q0 += (-qb * gx - qc * gy - q3 * gz);
  q1 += (qa * gx + qc * gz - q3 * gy);
  q2 += (qa * gy - qb * gz + q3 * gx);
  q3 += (qa * gz + qb * gy - qc * gx);

  recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm;
  q1 *= recipNorm;
  q2 *= recipNorm;
  q3 *= recipNorm;
  anglesComputed = 0;
}


// 역제곱근 함수
float Adafruit_Mahony::invSqrt(float x) {
  float halfx = 0.5f * x;
  union {
    float f;
    long i;
  } conv = {x};
  conv.i = 0x5f3759df - (conv.i >> 1);
  conv.f *= 1.5f - (halfx * conv.f * conv.f);
  conv.f *= 1.5f - (halfx * conv.f * conv.f);
  return conv.f;
}

//-------------------------------------------------------------------------------------------
//오일러각 변환
void Adafruit_Mahony::computeAngles() {
    // ICM20948 DMP 형식: q1_raw, q2_raw, q3_raw → q0 계산 + 축 재매핑
    // 현재 Mahony q0,q1,q2,q3는 이미 정규화된 float 사용
    
    float qw = q0;  // w
    float qx = q2;  // x (DMP qx = Mahony q2)
    float qy = q1;  // y (DMP qy = Mahony q1)  
    float qz = -q3; // z (DMP qz = -Mahony q3) - 축 반전!

    // Roll (deg)
    float t0 = +2.0f * (qw * qx + qy * qz);
    float t1 = +1.0f - 2.0f * (qx * qx + qy * qy);
    roll = atan2f(t0, t1) * 57.29578f;  // rad → deg

    // Pitch (deg) 
    float t2 = +2.0f * (qw * qy - qx * qz);
    t2 = constrain(t2, -1.0f, 1.0f);
    pitch = asinf(t2) * 57.29578f;

    // Yaw (deg)
    float t3 = +2.0f * (qw * qz + qx * qy);
    float t4 = +1.0f - 2.0f * (qy * qy + qz * qz);
    yaw = atan2f(t3, t4) * 57.29578f;

    // Gravity vector (기존 유지)
    grav[0] = 2.0f * (q1 * q3 - q0 * q2);
    grav[1] = 2.0f * (q0 * q1 + q2 * q3);
    grav[2] = 2.0f * (q0 * q0 - 0.5f + q3 * q3);
    
    anglesComputed = 1;
}
