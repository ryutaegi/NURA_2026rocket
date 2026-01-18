#include "PIDController.h"

PIDController::PIDController(float kpVal, float kiVal, float kdVal) 
  : kp(kpVal), ki(kiVal), kd(kdVal),
    previousError(0.0f), integral(0.0f), dt(0.02f), previousTime(0) {
}

float PIDController::compute(float target, float current) {
  unsigned long now = millis();
  dt = (now - previousTime) / 1000.0f;
  if (dt <= 0.0f) dt = 0.02f;
  previousTime = now;

  float error = target - current;
  integral += error * dt;
  float derivative = (error - previousError) / dt;

  float output = kp * error + ki * integral + kd * derivative;
  previousError = error;
  
  return constrain(output, -180.0f, 180.0f);
}

void PIDController::reset() {
  integral = 0.0f;
  previousError = 0.0f;
}

void PIDController::setGains(float kpVal, float kiVal, float kdVal) {
  kp = kpVal;
  ki = kiVal;
  kd = kdVal;
}

