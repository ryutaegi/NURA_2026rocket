#ifndef EARTH_FRAME_H
#define EARTH_FRAME_H

#include <Arduino.h>

struct FlightData {
  float roll;   // Earth 기준 Roll (deg)
  float pitch;  // Earth 기준 Pitch (deg)
  float yaw;    // Earth 기준 Yaw (deg) - 현재 계산 안 해서 0
};

extern FlightData flight;

// DCM 업데이트 (gyro: deg/s, dt: s)
void updateDcm(float gx, float gy, float gz, float dt);

// DCM -> Euler (deg)
void getEulerFromDcm(float &rollEarth, float &pitchEarth);

// 한 번에 업데이트 + 저장 + (필요시) 출력
void updateAttitudeAndStore(float gx, float gy, float gz, float dt);

#endif