#include "pin.h"
#include "Adafruit_AHRS_Mahony.h"
#include "servo_driver.h"

Adafruit_Mahony mahony6; 
Adafruit_Mahony mahony9; 

float prevValue = 0.0f;         // 이전값 저장용
const float THRESHOLD = 0.1f;  // 무시 임계값
unsigned long lastMs = 0;
float diff = 0.0f;

// ======================= 자이로 캘리브레이션 변수 =======================
     // 가속도 바이어스 저장 변수 (단위: G)
float accelBiasX = -32.7527f;
float accelBiasY = -17.3548f;
float accelBiasZ = 49.4481f;

// 캘리브레이션 관련 설정
const int BIAS_SAMPLES = 20000; // 바이어스 측정을 위해 수집할 샘플 개수
bool isCalibrated = false;    // 캘리브레이션 완료 여부 플래그

static bool gyro_calibrating = false;
static float gx_bias = -0.3257f, gy_bias = -0.3807f, gz_bias = 0.5573f;
static float gx_sum = 0.0f, gy_sum = 0.0f, gz_sum = 0.0f;
float mag_off_x = 11.65f, mag_off_y = -30.7f, mag_off_z = -86.05f;
float mag_scale_x = 1, mag_scale_y = 1, mag_scale_z = 1;
static uint32_t gyro_sample_count = 0;
const uint32_t GYRO_CAL_SAMPLES = 100;
// 전역 변수 선언 필요
//  float diff_q1 = 0.0f, diff_q2 = 0.0f, diff_q3 = 0.0f;
//  float prev_q1 = 0.0f, prev_q2 = 0.0f, prev_q3 = 0.0f;
//  float q1_drift_total = 0.0f, q2_drift_total = 0.0f, q3_drift_total = 0.0f;
// float THRESHOLD_Q1 = 0.01f, THRESHOLD_Q2 = 0.01f, THRESHOLD_Q3 = 0.01f;

// ======================= IMU 설정 =======================
ICM_20948_I2C myICM;
// ======================= 사용자 설정 =======================
const uint32_t PRINT_PERIOD_MS = 50;

const float LPF_K = 1.0f;
static unsigned long last_correction_time = 0;
static float yaw_drift_total = 0.0f;
ImuData imuData = {};
FlightData flightData = {};
const float MAG_BIAS_X = 0.0f;
const float MAG_BIAS_Y = 0.0f;
const float MAG_BIAS_Z = 0.0f;

const float MAG_AINV[3][3] = {
  { 1.0f, 0.0f, 0.0f },
  { 0.0f, 1.0f, 0.0f },
  { 0.0f, 0.0f, 1.0f }
};

const float ACC_1G = 1.0f;
const float W_LPF_K = 0.15f;

// ======================= 내부 변수 =======================
float ax_f = 0, ay_f = 0, az_f = 0;
float gx_f = 0, gy_f = 0, gz_f = 0;
float mx_f = 0, my_f = 0, mz_f = 0;

uint32_t lastPrint = 0;

bool att_inited = false;

float wGyro_f = 0.0f;

uint32_t lastMicros = 0;

float earth_roll = 0.0f;
float earth_pitch = 0.0f;
float earth_yaw = 0.0f;
float sensor_yaw = 0.0f;

// ======================= I2C 스캔 함수 =======================


// ======================= IMU 초기화 =======================
bool initializeIMU() {

  WIRE_PORT.begin();

  delay(200);

  return true;
}

// ======================= 유틸 =======================
static inline float deg2rad(float d) {
  return d * (PI / 180.0f);
}
static inline float rad2deg(float r) {
  return r * (180.0f / PI);
}

// ======================= 축 매핑 =======================
static inline float ACC_X() {
  return myICM.accX()-accelBiasX;
}
static inline float ACC_Y() {
  return myICM.accY()-accelBiasY;
}
static inline float ACC_Z() {
  return myICM.accZ()-accelBiasZ;
}

// ======================= 축 매핑 (바이어스 적용) =======================
static inline float GYR_X_DPS() {
  return myICM.gyrX() - gx_bias;
}
static inline float GYR_Y_DPS() {
  return myICM.gyrY() - gy_bias;
}
static inline float GYR_Z_DPS() {
  return myICM.gyrZ() - gz_bias;
}


static inline float MAG_X() {
  return myICM.magX();
}
static inline float MAG_Y() {
  return myICM.magY();
}
static inline float MAG_Z() {
  return myICM.magZ();
}
void getKPKD(float vel, float& kp, float& kd) {
  const float V_BASE = 2000.0f;

  const float KP_BASE = 2.7958f;
  const float KD_BASE = 0.3371f;

  if (vel <= 100.0f) {
    vel = 1.0f;
  }

  kp = KP_BASE * (V_BASE * V_BASE) / (vel * vel);
  kd = KD_BASE * (V_BASE * V_BASE) / (vel * vel);
}

// ======================= 메인 IMU 처리 =======================
void processIMU() {
  uint32_t nowUs = micros();
  float dt = (nowUs - lastMicros) * 1e-6f;
  lastMicros = nowUs;
  if (dt <= 0.0f || dt > 0.2f) return;

  float ax = ACC_X();
  float ay = ACC_Y();
  float az = ACC_Z();

  float gx = deg2rad(GYR_X_DPS());
  float gy = deg2rad(GYR_Y_DPS());
  float gz = deg2rad(GYR_Z_DPS());

  float mx = MAG_X();
  float my = -MAG_Y();
  float mz = -MAG_Z();

const float NOISE_THRESHOLD = deg2rad(15.0f); 
  if (abs(gx) < NOISE_THRESHOLD) gx = 0.0f;
  if (abs(gy) < NOISE_THRESHOLD) gy = 0.0f;
  if (abs(gz) < NOISE_THRESHOLD) gz = 0.0f;

  mx = (mx - mag_off_x) * mag_scale_x;
  my = (my - mag_off_y) * mag_scale_y;
  mz = (mz - mag_off_z) * mag_scale_z;

  ax_f += LPF_K * (ax - ax_f);
  ay_f += LPF_K * (ay - ay_f);
  az_f += LPF_K * (az - az_f);

  gx_f += LPF_K * (gx - gx_f);
  gy_f += LPF_K * (gy - gy_f);
  gz_f += LPF_K * (gz - gz_f);

  mx_f += LPF_K * (mx - mx_f);
  my_f += LPF_K * (my - my_f);
  mz_f += LPF_K * (mz - mz_f);

  imuData.ax = ax;  // 저장소는 m/s^2
  imuData.ay = ay;
  imuData.az = az;

  imuData.gx = rad2deg(gx);  // 라디안을 도(deg)로 변환
  imuData.gy = rad2deg(gy);
  imuData.gz = rad2deg(gz);




  mahony6.updateIMU(gx, gy, gz, ax, ay, az, dt);
  mahony6.computeAngles();
   




  sensor_yaw = mahony6.yaw;


  mahony9.update(gx, gy, gz, ax, ay, az, mx, my, mz, dt);
  mahony9.computeAngles();
  // 누적값 계산
  unsigned long now = millis();
  if (now - lastMs >= 5) {  // 5ms마다 실행
    lastMs = now;
  diff = sensor_yaw - prevValue;
    if (abs(diff) > THRESHOLD) {
      diff = 0.0f;  
    }
  prevValue = sensor_yaw;

  }
 //누적값 제거
  yaw_drift_total = yaw_drift_total + diff;           
  sensor_yaw = sensor_yaw - yaw_drift_total;
  sensor_yaw= wrap360_deg(sensor_yaw);



 // -180~180 변환
  if (sensor_yaw > 180.00f) {
    sensor_yaw -= 360.0f; 
  }
  
  flightData.filterRoll = sensor_yaw;

  float earth_roll = mahony9.q1;  
  float earth_pitch = mahony9.q2;
  float earth_yaw = mahony9.q3;


  flightData.roll =  earth_roll;
  flightData.pitch = earth_pitch;
  flightData.yaw = earth_yaw;
 


// // =========================================================
// // 2. 가속도 바이어스 캘리브레이션 함수 (전역 스코프에 추가)
// // =========================================================
// // 주의: 기체가 완벽히 정지해 있고, 수평을 유지한 상태에서 실행해야 합니다.
// // 가정: 로켓이 똑바로 서 있을 때 Z축 방향이 하늘(또는 땅)을 향해 중력 1G를 받는다고 가정.

//   float sumX = 0, sumY = 0, sumZ = 0;
//   int validSamples = 0;

//   Serial.println(F("가속도 센서 바이어스 캘리브레이션 시작..."));
//   Serial.println(F("경고: 기체를 절대로 움직이지 마세요."));

//   while (validSamples < BIAS_SAMPLES) {
//     if (myICM.dataReady()) {
//       myICM.getAGMT(); // 센서 데이터 읽기
      
//       // 단위를 G(중력가속도)로 변환하여 누적. 
//       // (센서 설정에 따라 1000.0f 등 스케일 팩터로 나누어야 할 수 있음)
//       // 현재 코드의 단위 체계가 밀리-지(mG)라면 1000으로 나누고, 이미 G라면 그대로 사용.
//       sumX += myICM.accX(); 
//       sumY += myICM.accY();
//       sumZ += myICM.accZ();
      
//       validSamples++;
//       // 진행 상황을 점으로 표시 (100번마다)
//       if (validSamples % 100 == 0) {
//         Serial.print(".");
//       }
//     }
//     delay(5); // 센서의 샘플링 속도(예: 200Hz)에 맞춘 대기 시간
//   }

//   // 평균값 계산
//   accelBiasX = sumX / BIAS_SAMPLES;
//   accelBiasY = sumY / BIAS_SAMPLES;
  
//   // Z축 보정: 
//   // 정지 상태에서 Z축이 하늘을 향한다면 중력(1G)이 측정되므로, 이 1G를 빼서 순수 바이어스만 남깁니다.
//   // 만약 기체 방향이나 센서 장착 방향에 따라 Z축이 아래를 향해 -1G가 찍힌다면 +1.0f를 해야 합니다.
//   accelBiasZ = (sumZ / BIAS_SAMPLES) - 1.0f; 

//   isCalibrated = true;
//   Serial.println(F("\n바이어스 캘리브레이션 완료!"));
//   Serial.print("Bias X: "); Serial.print(accelBiasX, 4); Serial.println(" G");
//   Serial.print("Bias Y: "); Serial.print(accelBiasY, 4); Serial.println(" G");
//   Serial.print("Bias Z-: "); Serial.print(accelBiasZ-980.665f, 4); Serial.println(" G");

//     Serial.print(0.5); Serial.print(",");
//    Serial.print(-0.5); Serial.print(",");
// //   Serial.print(",");
//      Serial.print(imuData.ax, 4);
// Serial.print(",");
//   Serial.print(imuData.ay, 4);
// Serial.print(",");
//   Serial.println(imuData.az-980.665f, 4);
//     Serial.print(mahony9.q1, 4);
// Serial.print(",");
//   Serial.print(mahony9.q2, 4);
// Serial.print(",");
//   Serial.print(mahony9.q3, 4);Serial.print(",");
//     Serial.println( flightData.filterRoll,6); 

  //     // ======================= 자이로 바이어스 측정 =======================
  // if (Serial.available() > 0) {
  //     String input = Serial.readStringUntil('\n');
  //     input.trim();
  //     if (input == "cal") {  // 시리얼 모니터에 "cal" 입력
  //         Serial.println("자이로 바이어스측정");
  //         gyro_calibrating = true;
  //         gyro_sample_count = 0;
  //         gx_sum = 0.0f; gy_sum = 0.0f; gz_sum = 0.0f;
  //     }
  // }

  // if (gyro_calibrating) {
  //     float gx_raw = GYR_X_DPS();
  //     float gy_raw = GYR_Y_DPS();
  //     float gz_raw = GYR_Z_DPS();

  //     gx_sum += gx_raw;
  //     gy_sum += gy_raw;
  //     gz_sum += gz_raw;

  //     gyro_sample_count++;

  //     if (gyro_sample_count >= GYRO_CAL_SAMPLES) {
  //         gx_bias = gx_sum / GYRO_CAL_SAMPLES;
  //         gy_bias = gy_sum / GYRO_CAL_SAMPLES;
  //         gz_bias = gz_sum / GYRO_CAL_SAMPLES;

  //         Serial.println("=== 자이로 캘리브레이션 완료 ===");
  //         Serial.print("GX Bias: "); Serial.println(gx_bias, 4);
  //         Serial.print("GY Bias: "); Serial.println(gy_bias, 4);
  //         Serial.print("GZ Bias: "); Serial.println(gz_bias, 4);
  //         Serial.println("이제 이 값을 사용해 자이로 데이터를 보정하세요!");

  //         gyro_calibrating = false;
  //     }
  //     return;  // 캘리브레이션 중에는 일반 IMU 처리 스킵
  // }

//마그네토미터 바이어스

// //--- [2] 마그네토미터 30초 캘리브레이션 ---
//   float m_min[3] = {9999, 9999, 9999};
//   float m_max[3] = {-9999, -9999, -9999};
  
//   Serial.println(">>> 30초간 센서를 모든 방향(8자)으로 돌리세요!");
//   uint32_t start_ms = millis();
  
//   while (millis() - start_ms < 60000) {
//     // 센서에서 현재 raw 자기장 값을 읽어옴 (변수명 mx, my, mz는 센서 읽기 값)
//     // mpu.getMagnetometer(&mx, &my, &mz); 

//     float m_raw[3] = {mx, my, mz}; 
//     for (int i = 0; i < 3; i++) {
//       if (m_raw[i] < m_min[i]) m_min[i] = m_raw[i];
//       if (m_raw[i] > m_max[i]) m_max[i] = m_raw[i];
//     }
//     delay(10); // 100Hz 샘플링
//   }

//   // 보정값(Hard-Iron & Soft-Iron) 계산
//   mag_off_x = (m_max[0] + m_min[0]) / 2.0f;
//   mag_off_y = (m_max[1] + m_min[1]) / 2.0f;
//   mag_off_z = (m_max[2] + m_min[2]) / 2.0f;

//   float avg_delta = ((m_max[0]-m_min[0]) + (m_max[1]-m_min[1]) + (m_max[2]-m_min[2])) / 3.0f;
//   mag_scale_x = avg_delta / (m_max[0] - m_min[0]);
//   mag_scale_y = avg_delta / (m_max[1] - m_min[1]);
//   mag_scale_z = avg_delta / (m_max[2] - m_min[2]);

//   Serial.println(">>> 캘리브레이션 완료!");
//   Serial.print("Offset: "); Serial.print(mag_off_x); Serial.print(", "); Serial.print(mag_off_y); Serial.print(", "); Serial.println(mag_off_z);

  uint32_t nowMs = millis();
  if (nowMs - lastPrint >= PRINT_PERIOD_MS) {
    lastPrint = nowMs;


 
  }
  flightData.imu = imuData;
  
}