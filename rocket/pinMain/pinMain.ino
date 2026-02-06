#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include "pin.h"           // 상보필터/IMU 처리
#include "PIDController.h" // PID compute

// ======================= PCA9685 설정 =======================
Adafruit_PWMServoDriver pca9685 = Adafruit_PWMServoDriver(0x40);

static const uint16_t PCA_FREQ_HZ = 50;

static const uint8_t  MOTOR_CH1   = 0;
static const uint8_t  MOTOR_CH2   = 1;

static const uint16_t SERVO_MIN_US = 500;
static const uint16_t SERVO_MAX_US = 2500;
static const float    SERVO_NEUTRAL_DEG = 90.0f;

// [설정] 서보 물리적 제한 각도 (중립 기준 ±10도)
static const float    MAX_SERVO_LIMIT = 10.0f; 

// [설정] PID 최대 출력
static const float    PID_OUTPUT_MAX  = 90.0f;

static const float MOTOR_DIR = +1.0f;

// ======================= 발사 감지 설정 =======================
// 가속도 임계값 (mg 단위). 1000 = 1G. 
// 로켓 모터 추력은 보통 5G 이상이므로 3G(3000) 이상이면 발사로 간주
static const float LAUNCH_THRESHOLD_MG = 3000.0f; 
bool isFlying = false;  // 비행 상태 플래그

// ======================= PID 설정 =======================
PIDController pid(1.2f, 0.01f, 0.08f);

// ======================= 타이밍 =======================
static uint32_t lastPidUs = 0;
static uint32_t lastDbgMs = 0;

// ======================= 유틸 =======================
static inline float rad2deg_f(float r){ return r * 180.0f / PI; }

static inline uint16_t usToTicks(uint16_t us){
  float ticks = (float)us * 4096.0f / 20000.0f;
  if (ticks < 0) ticks = 0;
  if (ticks > 4095) ticks = 4095;
  return (uint16_t)(ticks + 0.5f);
}

static void writeServoDeg(uint8_t ch, float deg){
  if (deg < 0.0f) deg = 0.0f;
  if (deg > 180.0f) deg = 180.0f;

  float us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * (deg / 180.0f);
  uint16_t ticks = usToTicks((uint16_t)us);
  
  pca9685.setPWM(ch, 0, ticks);
}

static inline float wrap360_deg(float d){
  while (d < 0.0f) d += 360.0f;
  while (d >= 360.0f) d -= 360.0f;
  return d;
}

float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  WIRE_PORT.begin();
  WIRE_PORT.setClock(400000);

  pca9685.begin();
  pca9685.setPWMFreq(PCA_FREQ_HZ);
  delay(10);
  
  writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG);
  writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG);

  bool ok = false;
  while(!ok){
    imu.begin(WIRE_PORT, AD0_VAL);
    if (imu.status == ICM_20948_Stat_Ok) {
      ok = true;
      imu.startupMagnetometer();
    } else {
      Serial.print("IMU init failed: ");
      Serial.println(imu.statusString());
      delay(500);
    }
    
    ICM_20948_fss_t myFSS; // 구조체 선언(가속도,자이로 범위설정 위함)

// 가속도 범위 설정 (gpm2, gpm4, gpm8, gpm16 중 선택)
myFSS.a = gpm16;       // ±16g로 설정

// 자이로 범위 설정 (dps250, dps500, dps1000, dps2000 중 선택)
myFSS.g = dps2000;    // ±2000dps로 설정

// 센서에 적용 
imu.setFullScale((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), myFSS);
  }
 

  lastMicros = micros();
  lastPidUs = micros();
  pid.reset();

  Serial.println(F("WAITING FOR LAUNCH... (Accel > 3G)"));
}

void loop() {
  if (!imu.dataReady()) return;
  imu.getAGMT(); // 데이터 읽기

  processIMU();  // 상보필터 업데이트

  static float last_yaw_deg = 0.0f;
  
  // ================= 1. 발사 감지 로직 =================
  if (!isFlying) {
    // 가속도 벡터 합 크기 계산 (단위: mg)
    float ax = imu.accX();
    float ay = imu.accY();
    float az = imu.accZ();
    float accelMag = sqrt(ax*ax + ay*ay + az*az);

    // 임계값 초과 시 발사로 간주
    if (accelMag > LAUNCH_THRESHOLD_MG) {
      isFlying = true;
      pid.reset(); // [중요] 비행 시작 직전에 PID 초기화 (I항 누적 방지)
      Serial.println(F("LAUNCH DETECTED! PID STARTED."));
    } else {
      // 대기 중에는 서보 중립 유지
      writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG);
      writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG);
      
      // 상보필터용 yaw 초기값 계속 갱신 (드리프트 방지)
      last_yaw_deg = wrap360_deg(rad2deg_f(yaw_est));
      return; 
    }
  }

  // ================= 2. 비행 중 제어 로직 =================
  
  uint32_t nowUs = micros();
  float dt = (nowUs - lastPidUs) * 1e-6f;
  lastPidUs = nowUs;

  if (dt <= 0.0f || dt > 0.2f) {
    // dt 오류 시 현상 유지 혹은 중립
    return;
  }

  // 목표: 각속도 0 (스핀 억제)
  float gyroZ_dps = rad2deg_f(gz_f);
  float target_dps = 0.0f;
  float error = (target_dps - gyroZ_dps);

  // (옵션) Yaw 각도 변화량에 따른 데드존
  float yaw_deg = wrap360_deg(rad2deg_f(yaw_est));
  float yaw_change = yaw_deg - last_yaw_deg;
  if (yaw_change > 180.0f) yaw_change -= 360.0f;
  if (yaw_change < -180.0f) yaw_change += 360.0f;
  last_yaw_deg = yaw_deg;
  
  const float DEAD_ZONE_DEG = 0.99f;
  if (fabsf(yaw_change) < DEAD_ZONE_DEG) {
    error = 0.0f; 
  }

  // PID 계산
  float pidOut = pid.compute(error, dt);

  // 맵핑 계산 (전체 출력 -> ±10도)
  float targetMin = SERVO_NEUTRAL_DEG - MAX_SERVO_LIMIT; // 80.0
  float targetMax = SERVO_NEUTRAL_DEG + MAX_SERVO_LIMIT; // 100.0

  float servoDeg = fmap(pidOut * MOTOR_DIR, -PID_OUTPUT_MAX, PID_OUTPUT_MAX, targetMin, targetMax);
  
  // 최종 안전 범위 clamp
  if (servoDeg < targetMin) servoDeg = targetMin;
  if (servoDeg > targetMax) servoDeg = targetMax;
  
  flightData.servoDegree = servoDeg; 

  // 모터 출력
  writeServoDeg(MOTOR_CH1, servoDeg);
  writeServoDeg(MOTOR_CH2, servoDeg);

  // ====== 시리얼 출력 ======
  uint32_t nowMs = millis();
  if (nowMs - lastDbgMs >= 50) {
    lastDbgMs = nowMs;
    // Serial.print("G:"); Serial.print(gyroZ_dps);
    // Serial.print(" S:"); Serial.println(servoDeg);
  }


}