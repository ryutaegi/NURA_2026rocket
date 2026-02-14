#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <float.h>
#include "pin.h"           // 상보필터/IMU 처리
#include "PIDController.h" // PID compute
#include "servo_driver.h"

#define PIN_CONNECT_DETECT 2

// [Spike Filter 변수]
static int      spikeCounter = 0;
static const int MAX_SPIKE_COUNT = 4; // n회 이상 튀면 FLT_MAX 처리

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

static const float    SERVO_NEUTRAL_DEG1 = 65.0f;  //검정핀
static const float    SERVO_NEUTRAL_DEG2 = 104.0f;  //흰색핀

// [설정] 서보 물리적 제한 각도
static const float    MAX_SERVO_LIMIT = 90.0f; 

// [설정] PID 최대 출력
static const float    PID_OUTPUT_MAX  = 90.0f;

static const float MOTOR_DIR = +1.0f;

// 1. 단축 거리 추가 (static 변수 loop() 맨 위)
static float prev_yaw = 0.0f;


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

// AtoB 데이터 UART송신(추가)
// 패킷 내용
static const uint8_t SYNC1 = 0xA5;
static const uint8_t SYNC2 = 0x5A;
static const uint8_t VER   = 1;
static const uint8_t MSG   = 0x21;
static const uint8_t LEN   = 20;

static uint16_t g_seq = 0;

// ====== CRC16 CCITT-FALSE ======
static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

// 반올림
static inline int32_t iround(float x) { return (x >= 0.0f) ? (int32_t)(x + 0.5f) : (int32_t)(x - 0.5f); }

static inline int16_t s16_scale(float x, float scale) {
  int32_t v = iround(x * scale);
  if (v > 32767) v = 32767;
  if (v < -32768) v = -32768;
  return (int16_t)v;
}

static inline void push_u16_le(uint8_t* buf, int& idx, uint16_t v) {
  buf[idx++] = (uint8_t)(v & 0xFF);
  buf[idx++] = (uint8_t)((v >> 8) & 0xFF);
}
static inline void push_i16_le(uint8_t* buf, int& idx, int16_t v) { push_u16_le(buf, idx, (uint16_t)v); }
static inline void push_u32_le(uint8_t* buf, int& idx, uint32_t v) {
  buf[idx++] = (uint8_t)(v & 0xFF);
  buf[idx++] = (uint8_t)((v >> 8) & 0xFF);
  buf[idx++] = (uint8_t)((v >> 16) & 0xFF);
  buf[idx++] = (uint8_t)((v >> 24) & 0xFF);
}

// ====== Send function ======
void sendAtoB() {
  uint8_t buf[64];
  int idx = 0;

  // SYNC
  buf[idx++] = SYNC1;
  buf[idx++] = SYNC2;

  // Header
  buf[idx++] = VER;
  buf[idx++] = MSG;
  buf[idx++] = LEN;
  push_u16_le(buf, idx, g_seq++);
  push_u32_le(buf, idx, flightData.timeMs);

  // Payload (10 * int16 = 20 bytes)
  // accel: m/s^2 * 10

  push_i16_le(buf, idx, flightData.imu.ax);//s16_scale(flightData.imu.ax, 100.0f));
  push_i16_le(buf, idx, flightData.imu.ay);
  push_i16_le(buf, idx, flightData.imu.az); //s16_scale(flightData.imu.az, 100.0f));
  // gyro: deg/s * 10
  push_i16_le(buf, idx, s16_scale(flightData.imu.gx, 10.0f));
  push_i16_le(buf, idx, s16_scale(flightData.imu.gy, 10.0f));
  push_i16_le(buf, idx, s16_scale(flightData.imu.gz, 10.0f));

  // angles: deg * 100
  push_i16_le(buf, idx, s16_scale(flightData.roll,       100.0f));
  push_i16_le(buf, idx, s16_scale(flightData.filterRoll, 100.0f));
  push_i16_le(buf, idx, s16_scale(flightData.pitch,      100.0f));
  push_i16_le(buf, idx, s16_scale(flightData.yaw,        100.0f));

  // CRC over [VER..PAYLOAD]
  uint16_t crc = crc16_ccitt(&buf[2], (size_t)(idx - 2));
  push_u16_le(buf, idx, crc);

  Serial3.write(buf, idx);
}

void setup() {
  Serial.begin(115200);
  Serial3.begin(115200); 
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
    Serial.println("sweepOnce");
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
      Serial.println(myICM.statusString());
      delay(500);
    }
    bool success = true;
  success &= (myICM.initializeDMP() == ICM_20948_Stat_Ok);

  success &= (myICM.enableDMPSensor(INV_ICM20948_SENSOR_GAME_ROTATION_VECTOR) == ICM_20948_Stat_Ok);
  success &= (myICM.enableDMPSensor(INV_ICM20948_SENSOR_ROTATION_VECTOR) == ICM_20948_Stat_Ok);
 
  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Quat6, 0) == ICM_20948_Stat_Ok); // 6축 데이터 속도
  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Quat9, 0) == ICM_20948_Stat_Ok); // 9축 데이터 속도
 
  success &= (myICM.setDMPODRrate(DMP_ODR_Reg_Cpass, 0) == ICM_20948_Stat_Ok);
  
  success &= (myICM.enableFIFO() == ICM_20948_Stat_Ok);
  success &= (myICM.enableDMP() == ICM_20948_Stat_Ok);
  success &= (myICM.resetDMP() == ICM_20948_Stat_Ok);
  success &= (myICM.resetFIFO() == ICM_20948_Stat_Ok);
  }

  lastMicros = micros();
  lastPidUs = micros();
  pid.reset();

  
}



void loop() {
  // ================= IMU 자동 복구 로직 =================
  
  // 1. 데이터 읽기 시도
  bool dataAvailable = false;
  if (myICM.dataReady()) {
    myICM.getAGMT();
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
        flightData.filterRoll = 0;
        flightData.imu.ax = 10000; 
        flightData.imu.ay = 10000;
        flightData.imu.az = 10000;
        Serial.println("센서 고장 판단 (3초)");
        //Serial.println(flightData.imu.az);
     }


  // 4. 센서가 비정상일 때 복구 시도
  if (!isImuHealthy) {
    // 안전을 위해 서보 중립
    flightData.filterRoll = 0;
    writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG1);
    writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG2);
   
    // 0.5초마다 재연결 시도
    if (millis() - lastResetAttemptMs > 500) {
      lastResetAttemptMs = millis();
      Serial.println(F("연결 끊김"));
      flightData.filterRoll = 0;
      // Wire 버스 리셋 시도 (선이 다시 꽂혔을 때를 대비)
        // 일부 라이브러리/보드에서 필요할 수 있음
       WIRE_PORT.end();
       WIRE_PORT.begin();
       WIRE_PORT.setClock(400000);

      if (configureIMU()) {
        Serial.println(F("IMU Recovered!"));
        isImuHealthy = true;
        frozenCount = 0;
        lastImuDataMs = millis();
        
       
      }
    }
    
  }

  
      
  
  
  // 데이터가 있으면 실행, 데이터 유무 상관 없이 센서통신낙하산보드로 전송
  if (dataAvailable) {


    processIMU();  // 상보필터 업데이트

    bool isSpike = (abs(myICM.accX()) > ACCEL_AXIS_LIMIT) || (abs(myICM.accY()) > ACCEL_AXIS_LIMIT) || (abs(myICM.accZ()) > ACCEL_AXIS_LIMIT);

      if (isSpike) {
        spikeCounter++;
        
        if (spikeCounter >= MAX_SPIKE_COUNT) {
          // [10회 이상 연속] -> 센서 고장으로 판단, 값을 100로 설정
            flightData.filterRoll = 0;
            writeServoDeg(MOTOR_CH1, SERVO_NEUTRAL_DEG1);
            writeServoDeg(MOTOR_CH2, SERVO_NEUTRAL_DEG2);
          
          
          flightData.imu.ax = 10000; 
          flightData.imu.ay = 10000;
          flightData.imu.az = 10000;
          Serial.println("센서 고장 판단 (2회 이상)");
        } 

      } 


    static float last_yaw_deg = 0.0f;

  
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


    // PID 계산
    float pidOut = pid.compute(error, dt);


    // yaw를 -180~180 범위로 정규화
  float yaw_deg = wrap720_deg(flightData.filterRoll);  // 0~360
  if (yaw_deg > 360.0f) yaw_deg -= 720.0f;  // -180~180 변환
  
  float diff = yaw_deg - prev_yaw;
  if (diff > 180.0f) yaw_deg -= 360.0f;    // 179° → -179°일 때 -181° → 181°로
  else if (diff < -180.0f) yaw_deg += 360.0f;  // 반대 경우 +360°

  prev_yaw = yaw_deg; 

    float servoOffset1, servoOffset2;
 if (yaw_deg <= 0.0f) {
    // -180 ~ 0 → -10 ~ 0
    servoOffset1 = fmap(yaw_deg, -360.0f, 0.0f, -MAX_SERVO_LIMIT, 0.0f);
    servoOffset2 = servoOffset1;  // 반대 방향 보정
  } else {
    // 0 ~ 180 → 0 ~ +10
    servoOffset1 = fmap(yaw_deg, 0.0f, 360.0f, 0.0f, MAX_SERVO_LIMIT);
    servoOffset2 = servoOffset1;  // 반대 방향 보정
  }
  
  // 최종 서보 각도 계산 및 클램프
  float servoDeg1 = SERVO_NEUTRAL_DEG1 + servoOffset1;
  float servoDeg2 = SERVO_NEUTRAL_DEG2 + servoOffset2;
 
  
  // 안전 범위 제한 (±10° 고정)
  servoDeg1 = constrain(servoDeg1, SERVO_NEUTRAL_DEG1 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG1 + MAX_SERVO_LIMIT);
  servoDeg2 = constrain(servoDeg2, SERVO_NEUTRAL_DEG2 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG2 + MAX_SERVO_LIMIT);
  
  // 서보 출력
  writeServoDeg(MOTOR_CH1, servoDeg1);
  writeServoDeg(MOTOR_CH2, servoDeg2);
  
  // 디버그 출력
  Serial.print("Yaw: "); Serial.print(yaw_deg, 1);
  Serial.print(" Servo1: "); Serial.print(servoDeg1, 1);
  Serial.print(" Servo2: "); Serial.println(servoDeg2, 1);

  }


  // 센서, 통신, 낙하산보드로 데이터 전송
  flightData.timeMs = millis();
  static uint32_t lastTx = 0;
  uint32_t now = millis();
  if (now - lastTx >= 10) {
    lastTx += 10;
    sendAtoB();
    
  }
  

  // ====== 시리얼 출력 ======
  // uint32_t nowMs = millis();
  // if (nowMs - lastDbgMs >= 50) {
  //   lastDbgMs = nowMs;
    // Serial.print("G:"); Serial.print(gyroZ_dps);
    // Serial.print(" S:"); Serial.println(servoDeg);
  //}
}