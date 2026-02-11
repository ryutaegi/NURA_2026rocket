#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

class PIDController {
public:
  // 생성자
  PIDController(float kp = 0.5f, float ki = 0.05f, float kd = 0.1f);
  
  // PID 제어 계산
  // error: 현재 오차값
  // dt: 시간 간격 (초 단위)
  // 반환값: 서보 명령값 (-90 ~ +90)
  float compute(float error, float dt);
  
  // 게인 변경
  void setGains(float kp, float ki, float kd);
  
  // 게인 조회
  float getKp() const { return kp; }
  float getKi() const { return ki; }
  float getKd() const { return kd; }
  
  // 적분/오차 리셋
  void reset();
  
private:
  float kp;             // Proportional gain
  float ki;             // Integral gain
  float kd;             // Derivative gain
  float integral;       // 누적된 적분값
  float prev_error;     // 이전 오차값
};

#endif