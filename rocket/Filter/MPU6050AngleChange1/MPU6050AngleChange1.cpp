#include "MPU6050AngleChange1.h"
#include <math.h>

MPU6050AngleChange1::MPU6050AngleChange1(unsigned long intervalMs, float alpha_val)
  : interval(intervalMs),
    // global/sensor-frame angles
    roll(0.0f), pitch(0.0f), yaw(0.0f),
    // gyro rates (deg/s, offset removed)
    gyroX(0.0f), gyroY(0.0f), gyroZ(0.0f),
    // accel-derived angles
    acc_roll(0.0f), acc_pitch(0.0f),
    // rocket-frame angles
    roll_rocket(0.0f), pitch_rocket(0.0f), yaw_rocket(0.0f),
    // raw accel (m/s^2)
    ax(0.0f), ay(0.0f), az(0.0f),
    // filter params / offsets
    alpha(alpha_val),
    gx_offset(0.0f), gy_offset(0.0f), gz_offset(0.0f),
    dt(0.005f),
    calibrated(false),
    preMillis(0) {}

bool MPU6050AngleChange1::begin() {
  if (!mpu.begin()) return false;
  setSensorRanges();
  return true;
}

void MPU6050AngleChange1::setSensorRanges() {
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void MPU6050AngleChange1::calibrateGyro(int samples) {
  float sum_x = 0, sum_y = 0, sum_z = 0;
  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sum_x += g.gyro.x * 180.0f / PI;
    sum_y += g.gyro.y * 180.0f / PI;
    sum_z += g.gyro.z * 180.0f / PI;
    delay(10);
  }
  gx_offset = sum_x / samples;
  gy_offset = sum_y / samples;
  gz_offset = sum_z / samples;
  calibrated = true;
}

bool MPU6050AngleChange1::update(float dt_in) {
  dt = dt_in;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // raw accel (m/s^2)
  ax = a.acceleration.x;
  ay = a.acceleration.y;
  az = a.acceleration.z;

  // roll (acc) [deg]
  acc_roll = atan2(ay / 9.81f,
                   sqrt((ax / 9.81f) * (ax / 9.81f) + (az / 9.81f) * (az / 9.81f))) * 180.0f / PI;

  // gyro rates (deg/s) with offset removal
  gyroX = (g.gyro.x * 180.0f / PI) - gx_offset;

  // complementary roll
  roll = alpha * (roll + gyroX * dt) + (1.0f - alpha) * acc_roll;

  // pitch (acc) [deg]
  acc_pitch = atan2(-ax / 9.81f,
                    sqrt((ay / 9.81f) * (ay / 9.81f) + (az / 9.81f) * (az / 9.81f))) * 180.0f / PI;

  gyroY = (g.gyro.y * 180.0f / PI) - gy_offset;

  // complementary pitch
  pitch = alpha * (pitch + gyroY * dt) + (1.0f - alpha) * acc_pitch;

  gyroZ = (g.gyro.z * 180.0f / PI) - gz_offset;

  // yaw integration (no mag correction)
  yaw = yaw + gyroZ * dt;

  return true;
}

bool MPU6050AngleChange1::convert(float rollSensor, float pitchSensor, float yawSensor) { // 로켓 프레임으로 값 바꿔주는 함수
  // NOTE:
  // - 로켓 롤축 == 센서 Z축 (yawSensor로 들어오는 값)
  // - 기존 작성된 매핑을 그대로 유지
  roll_rocket = yawSensor;
  pitch_rocket = pitchSensor;
  yaw_rocket = -rollSensor;
  return true;
}

void MPU6050AngleChange1::writeFlightData(FlightData &flight) const {
  // IMU raw/corrected
  flight.imu.ax = ax;
  flight.imu.ay = ay;
  flight.imu.az = az;

  flight.imu.gx = gyroX;
  flight.imu.gy = gyroY;
  flight.imu.gz = gyroZ;

  // ===== 각도 저장 규칙 =====
  // flight.roll/pitch/yaw  : 로켓 프레임 결과(roll축=센서Z)로 저장
  // flight.filter_roll     : 상보필터로 보정된 글로벌/센서 roll (deg)
  //
  // convert()의 매핑(roll_rocket=yaw, pitch_rocket=pitch, yaw_rocket=-roll)을 그대로 반영
  flight.roll = yaw;          // rocket roll  (== sensor/global yaw)
  flight.pitch = pitch;       // rocket pitch (== sensor/global pitch)
  flight.yaw = -roll;         // rocket yaw   (== -global roll)

  flight.filter_roll = roll;  // complementary filtered roll (global/sensor)

  // timestamp
  flight.timeMs = millis();
}
