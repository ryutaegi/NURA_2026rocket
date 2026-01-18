#ifndef MPU6050AngleChange1_H
#define MPU6050AngleChange1_H

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// =============================================================
// Flight enum / structs (공용 데이터 타입)
// - 센서 데이터는 구조체에 저장
// - 로켓 발사 단계는 enum으로 구분
// - 구조체 내부 값은 '.' 으로 접근 (중첩: 상위.하위.멤버)
// =============================================================

enum FlightState {
  STANDBY,     // 발사 전 (핀 꽂힘, 대기)
  LAUNCHED,    // 발사 감지됨 (핀 빠짐)
  POWERED,     // 모터 연소 중
  COASTING,    // 연소 종료 후 관성 상승
  APOGEE,      // 최고 고도 도달
  DESCENT,     // 하강 중
  LANDED       // 착지
};

struct BaroData {
  float pressure;      // 기압 (Pa 또는 hPa)
  float temperature;   // 온도 (°C)
  float altitude;      // 기준 대비 상대 고도 (m)
  float climbRate;     // 상승률 (m/s)
  uint32_t timeMs;     // 측정 시각 (millis)
};

struct GpsData {
  double latitude;     // 위도 (deg)
  double longitude;    // 경도 (deg)
  float altitude;      // GPS 고도 (m) 이건 정확도 낮아서 사용 x
  float speed;         // 지면 속도 (m/s)
  float heading;       // 진행 방향 (deg)
  uint8_t sats;        // 사용 중인 위성 수
  bool fix;            // 위치 신뢰 가능 여부
  uint32_t timeMs;     // 수신 시각 (millis)
};

struct ImuData {
  float ax;  // 가속도 X (m/s^2)
  float ay;  // 가속도 Y (m/s^2)
  float az;  // 가속도 Z (m/s^2)

  float gx;  // 각속도 X (deg/s)
  float gy;  // 각속도 Y (deg/s)
  float gz;  // 각속도 Z (deg/s)
};

struct FlightData {
  ImuData imu;         // IMU 원시/보정 데이터
  BaroData baro;       // 기압계 데이터
  GpsData gps;         // GPS 데이터

  float roll;          // 자세 추정 결과 (deg)  [로켓 프레임: 롤축=센서 Z]
  float filter_roll;   // 상보필터로 보정한 롤 각도 (deg) [글로벌/센서 프레임 roll]
  float pitch;         // 자세 추정 결과 (deg)
  float yaw;           // 자세 추정 결과 (deg)

  FlightState state;   // 현재 비행 상태
  uint32_t timeMs;     // 이 데이터의 기준 시각 (millis)
};

// =============================================================
// MPU6050AngleChange
// - 기존 코드 흐름 유지: update()로 각도 갱신, convert()로 로켓 프레임 매핑
// - 구조체 FlightData로 결과를 복사할 수 있도록 writeFlightData() 제공
// =============================================================

class MPU6050AngleChange1 {
public:
  MPU6050AngleChange1(unsigned long intervalMs = 50, float alpha = 0.98f);
  bool begin();
  void calibrateGyro(int samples = 200);

  // 기존 흐름 유지용
  bool update(float dt_in);
  bool convert(float rollSensor, float pitchSensor, float yawSensor);

  // 현재 내부 상태를 FlightData 구조체에 채움
  // - flight.roll/pitch/yaw: 로켓 프레임 결과(roll축=센서Z)
  // - flight.filter_roll: 상보필터로 보정된 글로벌/센서 roll
  // - flight.imu: (m/s^2, deg/s) 값 저장
  void writeFlightData(FlightData &flight) const;

  // getter
  float getRoll() const { return roll; }
  float getPitch() const { return pitch; }
  float getYaw() const { return yaw; }
  float getRoll_rocket() const { return roll_rocket; }
  float getPitch_rocket() const { return pitch_rocket; }
  float getYaw_rocket() const { return yaw_rocket; }
  float getAccRoll() const { return acc_roll; }
  float getAccPitch() const { return acc_pitch; }
  float getGyroRateX() const { return gyroX; }
  float getGyroRateY() const { return gyroY; }
  float getGyroRateZ() const { return gyroZ; }
  bool isCalibrated() const { return calibrated; }
  float getGyroOffsetX() const { return gx_offset; }
  float getGyroOffsetY() const { return gy_offset; }
  float getGyroOffsetZ() const { return gz_offset; }

private:
  Adafruit_MPU6050 mpu;
  unsigned long preMillis;
  unsigned long interval;

  // 자세 추정/센서 값
  float roll, pitch, yaw;
  float gyroX, gyroY, gyroZ;
  float acc_roll, acc_pitch;
  float roll_rocket, pitch_rocket, yaw_rocket;

  // raw accel (m/s^2)
  float ax, ay, az;

  // 필터 파라미터/캘리브레이션
  float alpha, gx_offset, gy_offset, gz_offset;
  float dt;
  bool calibrated;

  void setSensorRanges();
};

#endif
