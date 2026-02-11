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

#endif

