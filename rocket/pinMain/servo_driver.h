#pragma once
#include <Arduino.h>
#include <Adafruit_PWMServoDriver.h>

extern Adafruit_PWMServoDriver pca9685;

extern const float SERVO_NEUTRAL_DEG1;
extern const float SERVO_NEUTRAL_DEG2;
extern const float STARTUP_SWEEP_OFFSET_DEG;
extern const float STARTUP_SWEEP_STEP_DEG;

extern const uint8_t MOTOR_CH1;
extern const uint8_t MOTOR_CH2;

extern const uint16_t SERVO_MIN_US;
extern const uint16_t SERVO_MAX_US;

// ===== 유틸 =====
float wrap360_deg(float d);
float fmap(float x, float in_min, float in_max, float out_min, float out_max);

// ===== 서보 =====
uint16_t usToTicks(uint16_t us);
void writeServoDeg(uint8_t ch, float deg);
void sweepOnce(void);

// ===== IMU 설정 =====
bool configureIMU(void);

