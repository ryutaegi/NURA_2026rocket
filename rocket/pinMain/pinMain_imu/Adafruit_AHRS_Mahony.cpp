#include "Adafruit_AHRS_Mahony.h"
#include <math.h>
#include <Arduino.h>
//-------------------------------------------------------------------------------------------

#define DEFAULT_SAMPLE_FREQ 200.0f // sample frequency in Hz

// ★ 정적(약 1g) 판정 임계값 — 가속도계 단위에 맞춰 반드시 확인할 것.
//   아래 값은 "1g ≈ 1000" 단위(raw LSB 등)를 가정한 0.75g ~ 1.25g 창.
//   만약 m/s² 단위(1g ≈ 9.80665)로 넣는다면 약 7.4 ~ 12.3 으로 바꿔야 함.
//   이 창을 벗어나면 isStatic이 항상 false가 되어 가속도계 보정이 아예 안 걸림.
#define ACCEL_STATIC_MIN 750.0f
#define ACCEL_STATIC_MAX 1250.0f

float twoKpDef = (2.0f * 1.5f);     // 2 * proportional gain
float twoKiDef = (2.0f * 0.23f);    // 2 * integral gain
float twoKdDef = (2.0f * 0.005f);   // (D항 제거로 현재 미사용)
//-------------------------------------------------------------------------------------------
// 업데이트

Adafruit_Mahony::Adafruit_Mahony() : Adafruit_Mahony(twoKpDef, twoKiDef, twoKdDef) {}

Adafruit_Mahony::Adafruit_Mahony(float prop_gain, float int_gain, float der_gain) {
  twoKp = prop_gain; // 2 * proportional gain (Kp)
  twoKi = int_gain;  // 2 * integral gain (Ki)
  twoKd = der_gain;  // (D항 제거로 현재 미사용, 헤더/하위호환 위해 유지)
  q0 = 1.0f;
  q1 = 0.0f;
  q2 = 0.0f;
  q3 = 0.0f;
  last_halfex = last_halfey = last_halfez = 0.0f; // (D항 제거로 미사용)
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

  const float kp9 = (2.0f * 3.0f);   // 9축 전용 P 게인
  const float ki9 = (2.0f * 0.3f);   // 9축 전용 I 게인

  // 가속도 벡터의 크기(Norm) 계산
  float accelNorm = sqrtf(ax * ax + ay * ay + az * az);

  // 정적(약 1g) 판정: 추력/항력이 실리는 비행 구간에서는 가속도계 보정을 건너뛰고
  // 순수 자이로 적분만 수행 (가속도계가 중력을 못 재므로 자세가 오염되는 것을 방지)
  bool isStatic = (accelNorm > ACCEL_STATIC_MIN && accelNorm < ACCEL_STATIC_MAX);

  if (isStatic && !((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

    // 가속도 및 지자계 정규화
    recipNorm = 1.0f / accelNorm;
    ax *= recipNorm; ay *= recipNorm; az *= recipNorm;

    recipNorm = invSqrt(mx * mx + my * my + mz * mz);
    mx *= recipNorm; my *= recipNorm; mz *= recipNorm;

    // 쿼터니안 연산 보조 값
    q0q0 = q0 * q0; q0q1 = q0 * q1; q0q2 = q0 * q2; q0q3 = q0 * q3;
    q1q1 = q1 * q1; q1q2 = q1 * q2; q1q3 = q1 * q3;
    q2q2 = q2 * q2; q2q3 = q2 * q3; q3q3 = q3 * q3;

    // 지자계 보정 (Yaw 보정용)
    hx = 2.0f * (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2));
    hy = 2.0f * (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) + mz * (q2q3 - q0q1));
    bx = sqrtf(hx * hx + hy * hy);
    bz = 2.0f * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5f - q1q1 - q2q2));

    // 추정된 중력 및 지자계 방향
    halfvx = q1q3 - q0q2;
    halfvy = q0q1 + q2q3;
    halfvz = q0q0 - 0.5f + q3q3;

    halfwx = bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2);
    halfwy = bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3);   // ★ 수정: (0.5 - q1q1) → q0q1
    halfwz = bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2);

    // 오차 계산 (측정값과 추정값의 외적)
    halfex = (ay * halfvz - az * halfvy) + (my * halfwz - mz * halfwy);
    halfey = (az * halfvx - ax * halfvz) + (mz * halfwx - mx * halfwz);
    halfez = (ax * halfvy - ay * halfvx) + (mx * halfwy - my * halfwx);

    // PI 제어 보정값 적용 (D항 제거: 자이로가 rate를 직접 제공 + dt 나눗셈 노이즈 증폭 방지)
    if (ki9 > 0.0f) {
      integralFBx += ki9 * halfex * dt;
      integralFBy += ki9 * halfey * dt;
      integralFBz += ki9 * halfez * dt;
      gx += integralFBx; gy += integralFBy; gz += integralFBz;
    }
    gx += kp9 * halfex;
    gy += kp9 * halfey;
    gz += kp9 * halfez;
  }
  // else: 로켓 가속 중(isStatic == false)일 때는 gx, gy, gz 원본(자이로)만 사용됨

  // 2. 쿼터니안 업데이트 (자이로 적분) - 모든 상황에서 공통 수행
  float qa = q0, qb = q1, qc = q2;
  gx *= (0.5f * dt); gy *= (0.5f * dt); gz *= (0.5f * dt);

  q0 += (-qb * gx - qc * gy - q3 * gz);
  q1 += (qa * gx + qc * gz - q3 * gy);
  q2 += (qa * gy - qb * gz + q3 * gx);
  q3 += (qa * gz + qb * gy - qc * gx);

  // 정규화
  recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm; q1 *= recipNorm; q2 *= recipNorm; q3 *= recipNorm;
  anglesComputed = false;
}

//-------------------------------------------------------------------------------------------
// 마그네토미터 측정 없이
void Adafruit_Mahony::updateIMU(float gx, float gy, float gz, float ax,
                                float ay, float az, float dt) {
  float recipNorm;
  float halfvx, halfvy, halfvz;
  float halfex, halfey, halfez;
  float qa, qb, qc;

  // 가속도 벡터의 크기(Norm) 계산
  float accelNorm = sqrtf(ax * ax + ay * ay + az * az);

  // ★ update()와 동일한 정적 판정 게이트 추가.
  //   비행 중 가속도계는 중력이 아니라 추력/항력을 재므로, 이때 보정하면
  //   자세가 그 가속도 방향으로 끌려가 크게 오염됨. 정적일 때만 보정.
  bool isStatic = (accelNorm > ACCEL_STATIC_MIN && accelNorm < ACCEL_STATIC_MAX);

  if (isStatic && !((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {

    recipNorm = 1.0f / accelNorm;
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    halfvx = q1 * q3 - q0 * q2;
    halfvy = q0 * q1 + q2 * q3;
    halfvz = q0 * q0 - 0.5f + q3 * q3;

    halfex = (ay * halfvz - az * halfvy);
    halfey = (az * halfvx - ax * halfvz);
    halfez = (ax * halfvy - ay * halfvx);

    // PI 제어 (D항 제거)
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
  // else: 가속 중이면 자이로 원본만 사용

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
// 오일러각 변환
// 주의: 로켓이 수직에 가까우면 roll(asin) 추출이 짐벌락 근처라 조건수가 나빠짐.
//       롤 제어용 각도는 여기 Euler roll 말고 쿼터니안에서 롱축 기준으로 직접 뽑는 걸 권장.
//       축 리맵(qw=q0, qx=q2, qy=q1, qz=-q3)이 실제 마운팅과 맞는지도 확인 필요.
void Adafruit_Mahony::computeAngles() {

    float qw = q0;  // w
    float qx = q2;  // x
    float qy = q1;  // y
    float qz = -q3; // z

    // pitch (deg)
    float t0 = +2.0f * (qw * qx + qy * qz);
    float t1 = +1.0f - 2.0f * (qx * qx + qy * qy);
    pitch =  atan2f(t0, t1) * 57.29578f;
    pitch = -pitch;

    // roll (deg)
    float t2 = +2.0f * (qw * qy - qx * qz);
    t2 = constrain(t2, -1.0f, 1.0f);
    roll = asinf(t2) * 57.29578f;

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
