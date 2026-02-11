#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <float.h>
#include "pin.h"           // 상보필터/IMU 처리
#include "PIDController.h" // PID compute
#include "servo_driver.h"

#define PIN_CONNECT_DETECT 2

// [Spike Filter 변수]
static int      spikeCounter = 0;
static const int MAX_SPIKE_COUNT = 10; // n회 이상 튀면 FLT_MAX 처리

// 각 축별 임계값
static const float ACCEL_AXIS_LIMIT = 15500.0f; //15.5g



// ======================= PCA9685 설정 =======================
Adafruit_PWMServoDriver pca9685 = Adafruit_PWMServoDriver(0x40);
static const float STARTUP_SWEEP_OFFSET_DEG = 45.0f; 
static const float STARTUP_SWEEP_STEP_DEG   = 2.0f;  
static const uint16_t PCA_FREQ_HZ = 50;

static const uint8_t  MOTOR_CH1   = 0;
static const uint8_t  MOTOR_CH2   = 1;

static const uint16_t SERVO_MIN_US = 500;
static const uint16_t SERVO_MAX_US = 2500;

static const float    SERVO_NEUTRAL_DEG1 = 56.0f;  //검정핀
static const float    SERVO_NEUTRAL_DEG2 = 96.0f;  //흰색핀

// [설정] 서보 물리적 제한 각도
static const float    MAX_SERVO_LIMIT = 90.0f; 

// [설정] PID 최대 출력
static const float    PID_OUTPUT_MAX  = 90.0f;

static const float MOTOR_DIR = +1.0f;

// ======================= 발사 감지 설정 =======================

bool isFlying = false;  

// ======================= PID 설정 =======================
PIDController pid(1.2f, 0.01f, 0.08f);

// ======================= 타이밍 =======================
static uint32_t lastPidUs = 0;
static uint32_t lastDbgMs = 0;

// [추가 기능용 전역 변수]
static uint32_t lastImuDataMs = 0;      // 마지막으로 데이터 들어온 시간
static uint32_t lastResetAttemptMs = 0; // 마지막 리셋 시도 시간
static bool     isImuHealthy = false;   // 센서 건강 상태
static int      frozenCount = 0;        // 데이터 고정 카운트
static float    prevAx = 0, prevAy = 0, prevAz = 0; // 고정 감지용 이전 값

void setup() {
  Serial.begin(115200);
  delay(200);

  WIRE_PORT.begin();
  WIRE_PORT.setClock(400000);
  // 선이 빠졌을 때 Arduino가 멈추지 않게 함
   Wire.setWireTimeout(3000, true); 

  pca9685.begin();
  pca9685.setPWMFreq(PCA_FREQ_HZ);
  delay(10);
  
  writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG1);
  writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG2);

  pinMode(PIN_CONNECT_DETECT, INPUT);
  if (digitalRead(PIN_CONNECT_DETECT) == LOW) 
  {
    sweepOnce();
    delay(10);
  }
  else
  {
    writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG1);
    writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG2);
  }
  
  //  분리한 설정 함수 호출
  bool ok = false;
  while(!ok){
    if (configureIMU()) {
      ok = true;
      isImuHealthy = true;
      lastImuDataMs = millis();
    } else {
      Serial.print(F("IMU init failed: "));
      Serial.println(imu.statusString());
      delay(500);
    }
  }

  lastMicros = micros();
  lastPidUs = micros();
  pid.reset();

  
}

void loop() {
  // ================= IMU 자동 복구 로직 =================
  
  // 1. 데이터 읽기 시도
  bool dataAvailable = false;
  if (imu.dataReady()) {
    imu.getAGMT();
    dataAvailable = true;
    lastImuDataMs = millis();
    
  }

  // 3. 타임아웃 감지 (선이 뽑힘)
  // 500ms 동안 데이터가 안 들어오면 연결끊김으로 판단
  if (millis() - lastImuDataMs > 500) {
    isImuHealthy = false;
  }
  // 센서고장판단
     if (millis() - lastImuDataMs > 3000){
        float ax = 100; 
        float ay = 100;
        float az =100;
        Serial.println(ax);
        Serial.println(ay);
        Serial.println(az);
     }


  // 4. 센서가 비정상일 때 복구 시도
  if (!isImuHealthy) {
    // 안전을 위해 서보 중립
    writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG1);
    writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG2);
   
    // 0.5초마다 재연결 시도
    if (millis() - lastResetAttemptMs > 500) {
      lastResetAttemptMs = millis();
      Serial.println(F("연결 끊김"));
      
      // Wire 버스 리셋 시도 (선이 다시 꽂혔을 때를 대비)
       Wire.end(); // 일부 라이브러리/보드에서 필요할 수 있음
       Wire.begin();
       WIRE_PORT.setClock(400000);

      if (configureIMU()) {
        Serial.println(F("IMU Recovered!"));
        isImuHealthy = true;
        frozenCount = 0;
        lastImuDataMs = millis();
        
       
      }
    }
    return;
    

  }

  bool isSpike = (abs(imu.accX()) > ACCEL_AXIS_LIMIT) || (abs(imu.accY()) > ACCEL_AXIS_LIMIT) || (abs(imu.accZ()) > ACCEL_AXIS_LIMIT);

    if (isSpike) {
      spikeCounter++;
      
      if (spikeCounter >= MAX_SPIKE_COUNT) {
        // [2회 이상 연속] -> 센서 고장으로 판단, 값을 FLT_MAX로 설정

         writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG1);
         writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG2);
        
        
        float ax = 100; 
        float ay = 100;
        float az =100;
        Serial.println(ax);
        Serial.println(ay);
        Serial.println(az);
      } 
      else {
      
      }
    } 
   if(!isSpike) {
     spikeCounter = 0;
      // 유효 데이터 업데이트
       float ax = imu.accX();
       float ay = imu.accY();
       float az = imu.accZ();
    }
      
      
  
  // 데이터가 없으면 리턴 (위에서 dataReady 체크했으므로 여기서는 pass)
  if (!dataAvailable) return;

  // ================= 기존 로직 유지 =================

  processIMU();  // 상보필터 업데이트

  static float last_yaw_deg = 0.0f;
  
  // ================= 1. 발사 감지 로직 =================
  

  // ================= 2. 비행 중 제어 로직 =================
  
  uint32_t nowUs = micros();
  float dt = (nowUs - lastPidUs) * 1e-6f;
  lastPidUs = nowUs;

  if (dt <= 0.0f || dt > 0.2f) {
    // dt 오류 시 현상 유지 혹은 중립
    return;
  }

  // 목표: 각속도 0 (스핀 억제)
  float gyroZ_dps = rad2deg(gz_f);
  float target_dps = 0.0f;
  float error = (target_dps - gyroZ_dps);

  // (옵션) Yaw 각도 변화량에 따른 데드존
  float yaw_deg = wrap360_deg(rad2deg(yaw_est));
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

  // 맵핑 계산 (전체 출력 -> ±10도)  -> 여기부터 서보각도 수정해야함
  float targetMin1 = SERVO_NEUTRAL_DEG1 - MAX_SERVO_LIMIT; // 80.0
  float targetMax1 = SERVO_NEUTRAL_DEG1 + MAX_SERVO_LIMIT; // 100.0

  float targetMin2 = SERVO_NEUTRAL_DEG2 - MAX_SERVO_LIMIT; // 80.0
  float targetMax2 = SERVO_NEUTRAL_DEG2 + MAX_SERVO_LIMIT; // 100.0

  float servoDeg1 = fmap(pidOut * MOTOR_DIR, -PID_OUTPUT_MAX, PID_OUTPUT_MAX, targetMin1, targetMax1);
  float servoDeg2 = fmap(pidOut * MOTOR_DIR, -PID_OUTPUT_MAX, PID_OUTPUT_MAX, targetMin2, targetMax2);
  
  // 최종 안전 범위 clamp
  if (servoDeg1 < targetMin1) servoDeg1 = targetMin1;
  if (servoDeg1 > targetMax1) servoDeg1 = targetMax1;

  if (servoDeg2 < targetMin2) servoDeg2 = targetMin2;
  if (servoDeg2 > targetMax2) servoDeg2 = targetMax2;
  
  flightData.servoDegree = servoDeg1; 
  flightData.servoDegree = servoDeg2; 

  // 모터 출력
  writeServoDeg(MOTOR_CH1, servoDeg1);
  writeServoDeg(MOTOR_CH2, servoDeg2);

  // ====== 시리얼 출력 ======
  uint32_t nowMs = millis();
  if (nowMs - lastDbgMs >= 50) {
    lastDbgMs = nowMs;
    // Serial.print("G:"); Serial.print(gyroZ_dps);
    // Serial.print(" S:"); Serial.println(servoDeg);
  }
}