#ifndef FLIGHT_TYPES_H
#define FLIGHT_TYPES_H

#include <Arduino.h>

enum FlightState : uint8_t {
  STANDBY, LAUNCHED, POWERED, COASTING, APOGEE, DESCENT, LANDED
};

struct __attribute__((packed)) ImuData { float ax, ay, az; float gx, gy, gz; };

struct __attribute__((packed)) BaroData {
  float pressure;      // hPa
  float temperature;   // °C
  float altitude;      // m (상대고도)
  float climbRate;     // m/s
};

struct __attribute__((packed)) GpsData {
  int32_t latitudeE7;   // deg*1e7
  int32_t longitudeE7;  // deg*1e7
  float altitude;       // m (log only)
  float speed;          // m/s
  float heading;        // deg
  uint8_t sats;
  bool fix;
};

struct __attribute__((packed)) FlightData {
  ImuData imu;
  BaroData baro;
  GpsData gps;

  float roll;
  float filterRoll;
  float pitch;
  float yaw;

  float servoDegree;

  uint32_t baroTimeMs;   // B가 baro를 읽어 갱신한 시각(B millis)
  uint32_t gpsTimeMs;    // B가 gps(위치/속도 등)를 갱신한 시각(B millis)
  uint32_t aTimeMs; // A가 보낸 millis()
  uint32_t aRxTimeMs;  // B가 받은 시각(B millis)

  FlightState state;
  uint32_t timeMs;       // B 기준 시간(=millis)
};

struct JudgeCounters {            //카운터 초기화 위한 구조체
uint8_t powered  = 0;
uint8_t motorOver = 0;
uint8_t apogee   = 0;
uint8_t descent  = 0;
uint8_t count = 0;
};
enum DeployState : uint8_t {  //서보모터 이넘
  DEPLOY_IDLE = 0,            // 사출 대기
  DEPLOY_PUNCH,               // 카트리지 찌르기 (사출 시작)
  DEPLOY_LOCK,                // 찌른 상태 유지
  DEPLOY_DONE                 // 사출 완료 (재사출 방지)
};


struct DeployController {
  DeployState state;  // 현재 사출 단계
  bool deployed;      // 1회 사출 래치
};




#endif

