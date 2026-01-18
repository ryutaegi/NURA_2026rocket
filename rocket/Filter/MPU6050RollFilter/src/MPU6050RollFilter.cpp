#include "MPU6050RollFilter.h"

MPU6050RollFilter::MPU6050RollFilter(unsigned long intervalMs, float alphaVal) 
  : interval(intervalMs), 
    alpha(clamp01(alphaVal)), 
    roll(0.0f), 
    gyroXDeg(0.0f), 
    accRoll(0.0f),
    gyroOffsetDeg(0.0f),
    calibrated(false) {}

//MPU6050 센서를 초기화하고 정상 연결 여부 확인
bool MPU6050RollFilter::begin() {
  if (!mpu.begin()) return false;
  setSensorRanges(); //가속도, 자이로 범위 설정
  return true;
}

//setSensorRanges(): 센서의 측정 범위와 필터 대역폭을 설정
void MPU6050RollFilter::setSensorRanges() {
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G); //가속도 최대 +-8g
  mpu.setGyroRange(MPU6050_RANGE_500_DEG); //자이로 최대 +-500
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); //저역통과필터 대역폭 21Hz
}

//자이로 보정: 일정 시간 동안 평균값을 구해 오프셋 계산
void MPU6050RollFilter::calibrateGyro(int samples, unsigned long sampleDelayMs) {
  float sum = 0.0f;

  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp); //센서 데이터 읽기

    float gxDeg = g.gyro.x * 180.0f / PI; //rad/s-> deg/s로 변환 후 누적
    sum += gxDeg;

    delay(sampleDelayMs); 
  }

  gyroOffsetDeg = sum / samples;
  calibrated = true; //보정 완료
}

//내부에서 센서 직접 읽는 update
bool MPU6050RollFilter::update(float dtSeconds) {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  ImuData data;
  data.ax = a.acceleration.x;
  data.ay = a.acceleration.y;
  data.az = a.acceleration.z;

  data.gx = g.gyro.x * 180.0f / PI;
  data.gy = g.gyro.y * 180.0f / PI; 
  data.gz = g.gyro.z * 180.0f / PI;

  return update(data, dtSeconds);
}

//외부에서 구조체 ImuData 받아서 roll 계산
bool MPU6050RollFilter::update(const ImuData& data, float dt) {
  if (dt <= 0.0f) {
    dt = interval / 1000.0f;
  } //dt: 현재 업데이트 주기에서의 실제 경과 시간 (각속도 적분에 사용되는 시간 간격)

  float ax = data.ax / 9.81f;
  float ay = data.ay / 9.81f;
  float az = data.az / 9.81f;

  //가속도 기반 roll각도 계산
  accRoll = atan2(ay, sqrt(ax * ax + az * az)) * 180.0f / PI;

  //자이로 x축 회전 속도 계산 (센서 바이어스 제거된 실제 각속도)
  gyroXDeg = data.gx - gyroOffsetDeg;

  //보정된 roll 계산 (상보필터... 자이로 적분+가속도 보정)
  roll = alpha * (roll + gyroXDeg * dt) 
       + (1.0f - alpha) * accRoll;

  return true;
}