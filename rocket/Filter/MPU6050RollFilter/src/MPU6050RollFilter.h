#ifndef MPU6050ROLLFILTER_H
#define MPU6050ROLLFILTER_H

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <math.h>


//센서 데이터를 담을 구조체ImuData 정의
struct ImuData {
  float ax, ay, az; 
  float gx, gy, gz; //deg/s
};

//MPU6050 센서를 이용해서 roll 각도를 계산하는 필터 클래스
class MPU6050RollFilter {
public:
  MPU6050RollFilter(unsigned long intervalMs = 50, float alpha = 0.98f);   //생성자
  
  bool begin();     //센서 초기화
  void calibrateGyro(int samples = 200, unsigned long sampleDelayMs = 10);   //자이로 오프셋 보정
  
  bool update(const ImuData& data, float dtSeconds); //외부에서 센서값 입력받아 roll 계산
  bool update(float dtSeconds);

  //결과값 반환 함수들
  float getRoll() const { return roll; }
  float getAccRoll() const { return accRoll; }
  float getGyroRateDeg() const { return gyroXDeg; }
  float getGyroOffsetDeg() const {return gyroOffsetDeg; }

private:
  Adafruit_MPU6050 mpu;   //센서 객체
  unsigned long interval;   //갱신 간격

  float roll; //최종 추정된 roll각도 (deg)
  float gyroXDeg; //오프셋 제거된 자이로 각속도(deg/s)
  float accRoll;  //가속도 기반 roll각도 (deg)
  float alpha;  //보정필터 계수
  float gyroOffsetDeg;   //자이로 오프셋(초기값 보정용)
  bool calibrated;  //자이로 보정 여부

  void setSensorRanges();  //센서 범위 설정

  static float clamp01(float x) {
    if (x < 0.0f) return 0.0f;  //0보다 작으면 0으로 고정
    if (x > 1.0f) return 1.0f;  //1보다 크면 1로 고정
    return x;  //정상범위면 그대로 반환
  }
};

#endif