#include "EarthFrame.h"
#include <math.h>

// 전역 결과 변수
FlightData flight;

// DCM(Body -> Earth) 상태 (초기: 단위행렬)
static float dcmBodyToEarth[3][3] = {
  {1, 0, 0},
  {0, 1, 0},
  {0, 0, 1}
};

void updateDcm(float gx, float gy, float gz, float dt) {
  // 1) deg/s -> rad/s
  float p = gx * DEG_TO_RAD;
  float q = gy * DEG_TO_RAD;
  float r = gz * DEG_TO_RAD;

  // 2) Omega(ω): skew-symmetric matrix
  float omegaMat[3][3] = {
    { 0, -r,  q},
    { r,  0, -p},
    {-q,  p,  0}
  };

  // 3) Cdot = C * Omega
  float dcmDot[3][3] = {0};

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      for (int k = 0; k < 3; k++) {
        dcmDot[i][j] += dcmBodyToEarth[i][k] * omegaMat[k][j];
      }
    }
  }

  // 4) Forward Euler integration: C <- C + Cdot * dt
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      dcmBodyToEarth[i][j] += dcmDot[i][j] * dt;
    }
  }
}

void getEulerFromDcm(float &rollEarth, float &pitchEarth) {
  float pitchRad = -asinf(dcmBodyToEarth[2][0]);
  float rollRad  = atan2f(dcmBodyToEarth[2][1], dcmBodyToEarth[2][2]);

  rollEarth  = rollRad  * RAD_TO_DEG;
  pitchEarth = pitchRad * RAD_TO_DEG;
}

void updateAttitudeAndStore(float gx, float gy, float gz, float dt) {
  float rollEarth  = 0.0f;
  float pitchEarth = 0.0f;

  updateDcm(gx, gy, gz, dt);
  getEulerFromDcm(rollEarth, pitchEarth);

  // 구조체에 저장 
  flight.roll  = rollEarth;
  flight.pitch = pitchEarth;
  flight.yaw   = 0.0f; // yaw는 아직 계산 안 함
}