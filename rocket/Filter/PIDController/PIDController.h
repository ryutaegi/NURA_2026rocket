#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

#include <Arduino.h>

// 롤 안정화용 PID 컨트롤러 클래스
class PIDController {
public:
  PIDController(float kp = 2.0f, float ki = 0.1f, float kd = 0.5f);
  
  float compute(float target, float current);  // PID 계산 반환 (-180~180)
  void reset();                                // 적분/오차 리셋
  void setGains(float kp, float ki, float kd); // 게인 변경
  
  // 게인 조회
  float getKp() const { return kp; }
  float getKi() const { return ki; }
  float getKd() const { return kd; }

private:
  float kp, ki, kd;
  float previousError;
  float integral;
  float dt;
  unsigned long previousTime;
};

#endif
