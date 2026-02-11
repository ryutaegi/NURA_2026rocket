#include "PIDController.h"

// 생성자
PIDController::PIDController(float kpVal, float kiVal, float kdVal)
  : kp(kpVal), ki(kiVal), kd(kdVal), integral(0.0f), prev_error(0.0f) {
}

// PID 제어 계산 (기존 calculatePID() 함수와 동일)
float PIDController::compute(float error, float dt) {
  // 비례 제어 (P)
  float p_term = kp * error;
  
  // 적분 제어 (I)
  integral += error * dt;
  // 적분값 범위 제한 (-45 ~ +45)
  if (integral > 45.0f) integral = 45.0f;
  if (integral < -45.0f) integral = -45.0f;
  float i_term = ki * integral;
  
  // 미분 제어 (D)
  float derivative = (error - prev_error) / dt;
  float d_term = kd * derivative;
  
  // 전체 제어 출력
  float output = p_term + i_term + d_term;
  
  // 출력값 범위 제한 (-90 ~ +90)
  if (output > 90.0f) output = 90.0f;
  if (output < -90.0f) output = -90.0f;
  
  prev_error = error;
  
  return output;
}

// 게인 변경
void PIDController::setGains(float kpVal, float kiVal, float kdVal) {
  kp = kpVal;
  ki = kiVal;
  kd = kdVal;
}

// 적분/오차 리셋
void PIDController::reset() {
  integral = 0.0f;
  prev_error = 0.0f;
}
