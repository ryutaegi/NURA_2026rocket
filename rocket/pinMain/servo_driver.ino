#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>
#include "pin.h"

// ======================= 유틸 =======================
uint16_t usToTicks(uint16_t us){
  float ticks = (float)us * 4096.0f / 20000.0f;
  if (ticks < 0) ticks = 0;
  if (ticks > 4095) ticks = 4095;
  return (uint16_t)(ticks + 0.5f);
}

void writeServoDeg(uint8_t ch, float deg){
  if (deg < 0.0f) deg = 0.0f;
  if (deg > 180.0f) deg = 180.0f;

  float us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * (deg / 180.0f);
  uint16_t ticks = usToTicks((uint16_t)us);
  
  pca9685.setPWM(ch, 0, ticks);
}

float wrap360_deg(float d){
  while (d < 0.0f) d += 360.0f;
  while (d >= 360.0f) d -= 360.0f;
  return d;
}

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void sweepOnce()
{
  const float startDeg1 = SERVO_NEUTRAL_DEG1 - STARTUP_SWEEP_OFFSET_DEG; // 0
  const float endDeg1   = SERVO_NEUTRAL_DEG1 + STARTUP_SWEEP_OFFSET_DEG; // 180

  const float startDeg2 = SERVO_NEUTRAL_DEG2 - STARTUP_SWEEP_OFFSET_DEG; // 0
  const float endDeg2   = SERVO_NEUTRAL_DEG2 + STARTUP_SWEEP_OFFSET_DEG; // 180


  // 시작 위치(0도)로 이동 후 잠깐 대기
  writeServoDeg(MOTOR_CH1, startDeg1);
  writeServoDeg(MOTOR_CH2, startDeg2);
  delay(300);

  // 0 -> 180 스윕
  for (float d = startDeg1; d <= endDeg1; d += STARTUP_SWEEP_STEP_DEG) {
    writeServoDeg(MOTOR_CH1, d);
    // delay(15);
  }
  for (float d = startDeg2; d <= endDeg2; d += STARTUP_SWEEP_STEP_DEG) {
    writeServoDeg(MOTOR_CH2, d);
    // delay(15);
  }

  // 180 -> 90(중립) 복귀 (원하면 180->0까지 왕복도 가능)
  for (float d = endDeg1; d >= SERVO_NEUTRAL_DEG1; d -= STARTUP_SWEEP_STEP_DEG) {
    writeServoDeg(MOTOR_CH1, d);
    // delay(15);
  }
  for (float d = endDeg2; d >= SERVO_NEUTRAL_DEG2; d -= STARTUP_SWEEP_STEP_DEG) {
    writeServoDeg(MOTOR_CH2, d);
    // delay(15);
  }

  // 최종 중립 고정
  writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG1);
  writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG2);
  delay(300);
}

// [추가 기능] IMU 설정 로직을 함수로 분리 (Setup과 Loop에서 재사용하기 위해)
bool configureIMU() {
  imu.begin(WIRE_PORT, AD0_VAL);
  if (imu.status != ICM_20948_Stat_Ok) {
    return false;
  }
  
  imu.startupMagnetometer();

  ICM_20948_fss_t myFSS; 
  myFSS.a = gpm16;       
  myFSS.g = dps2000;    
  imu.setFullScale((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), myFSS);

  ICM_20948_dlpcfg_t myDLPF;   
  myDLPF.a = (ICM_20948_ACCEL_CONFIG_DLPCFG_e)6;   
  myDLPF.g = (ICM_20948_GYRO_CONFIG_1_DLPCFG_e)6;  
  imu.enableDLPF(ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr, true);
  imu.setDLPFcfg(ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr, myDLPF);
  
  return true;
}