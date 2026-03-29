#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <float.h>
#include <avr/wdt.h>

#include "pin.h"           // 상보필터/IMU 처리
#include "servo_driver.h"

#define PIN_CONNECT_DETECT 2


// float diff = 0.0f;
// float prevValue = 0.0f;         // 이전값 저장용
// const float THRESHOLD = 0.05f;
//누적값
float yaw_drift_total = 0.00f; 

static float yaw_lowpass = 0.0f;
static float drift_estimate = 0.0f;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
static uint32_t stable_count = 0;
static uint32_t last_stable_time = 0;

// [Spike Filter 변수]
static int      spikeCounter = 0;
static const int MAX_SPIKE_COUNT = 4; // n회 이상 튀면 FLT_MAX 처리
// 각 축별 임계값
static const float ACCEL_AXIS_LIMIT = 15500.0f; //15.5g

// ======================= PCA9685 설정 =======================
Adafruit_PWMServoDriver pca9685 = Adafruit_PWMServoDriver(0x40);
static const float STARTUP_SWEEP_OFFSET_DEG = 45.0f; 

static const uint16_t PCA_FREQ_HZ = 50;

static const uint8_t  MOTOR_CH1   = 0;
static const uint8_t  MOTOR_CH2   = 1;

static const uint16_t SERVO_MIN_US = 500;
static const uint16_t SERVO_MAX_US = 2500;

static const float   SERVO_NEUTRAL_DEG1 = 74.0f;  //흰
static const float   SERVO_NEUTRAL_DEG2 = 83.5f;  //검

// [설정] 서보 물리적 제한 각도
static const float    MAX_SERVO_LIMIT = 24.4f; 

// 이전 yaw  
static float prev_yaw = 0.0f;


// ======================= 타이밍 =======================
static uint32_t lastDbgMs = 0;

//기능고장 대처 
static uint32_t lastImuDataMs = 0;      // 마지막으로 데이터 들어온 시간
static uint32_t lastResetAttemptMs = 0; // 마지막 리셋 시도 시간
static bool     isImuHealthy = false;   // 센서 건강 상태

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


void softwareReset() {
  wdt_enable(WDTO_15MS);  // 15ms 후 리셋
  while (1) {}            // 대기 → WDT 트리거
}

// ===== B -> A reset status receiver =====    리셋 유무 받음
static const uint8_t B2A_SYNC1 = 0xB5;
static const uint8_t B2A_SYNC2 = 0x5B;
static const uint8_t B2A_VER   = 1;
static const uint8_t B2A_MSG   = 0x31;
static const uint8_t B2A_LEN   = 6;

void pollB2A(Stream& link) {
  enum { WAIT_S1, WAIT_S2, READ_HDR, READ_PAYLOAD } static st = WAIT_S1;

  static uint8_t hdr[5];
  static uint8_t payload[B2A_LEN];
  static uint8_t crcBytes[2];
  static uint8_t idx = 0;

  while (link.available()) {
    uint8_t b = link.read();

    switch (st) {
      case WAIT_S1:
        if (b == B2A_SYNC1) st = WAIT_S2;
        break;

      case WAIT_S2:
        if (b == B2A_SYNC2) {
          st = READ_HDR;
          idx = 0;
        } else st = WAIT_S1;
        break;

      case READ_HDR:
        hdr[idx++] = b;
        if (idx >= 5) {
          if (hdr[0] != B2A_VER || hdr[1] != B2A_MSG || hdr[2] != B2A_LEN) {
            st = WAIT_S1;
            break;
          }
          idx = 0;
          st = READ_PAYLOAD;
        }
        break;

      case READ_PAYLOAD:
        if (idx < B2A_LEN) {
          payload[idx++] = b;
        } else if (idx < B2A_LEN + 2) {
          crcBytes[idx - B2A_LEN] = b;
          idx++;
        }

        if (idx >= B2A_LEN + 2) {
          // ⚠ CRC 생략 버전 (디버깅용)
          if(payload[0] == 1)
            softwareReset();


          Serial.print("RECV B->A parachute=");
         

          st = WAIT_S1;
        }
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial3.begin(115200); 
  delay(100);

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
      Serial.println(myICM.statusString());
      delay(500);
    }

  
 
}
  // float prevValue = 0.0f;  
  // float diff = 0.0f;
  // flightData.diff_total = 0.0f;
}

void loop() {
  pollB2A(Serial3);
  // ================= IMU 자동 복구 =================
  
  //  데이터 읽기 시도
  bool dataAvailable = false;
  if (myICM.dataReady()) {
    myICM.getAGMT();
    dataAvailable = true;
    lastImuDataMs = millis();
    
  }

  // 타임아웃 감지 (선이 뽑힘)
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


  //  센서가 비정상일 때 복구 시도
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
      
        isImuHealthy = true;
        lastImuDataMs = millis();
        
       
      }
    }
    
  }
  
  // 데이터가 있으면 실행, 데이터 유무 상관 없이 센서통신낙하산보드로 전송
  if (dataAvailable) {


     processIMU();  // 상보필터 업데이트

  //   diff = flightData.filterRoll - prevValue;
  //   //3. 차이가 0.01 초과 시 무시 (diff = 0으로 처리)
  //   if (abs(diff) > THRESHOLD) {
  //     diff = 0.0f;  // 또는 prevValue 유지
  //   }
  //   flightData.diff_total = flightData.diff_total + diff;
  //   // 4. 원래값에서 보정
  //   flightData.filterRoll = flightData.filterRoll + flightData.diff_total;

  //   prevValue = flightData.filterRoll;


  // if (flightData.filterRoll > 180.00f) {
  //   flightData.filterRoll -= 360.0f;  // -180~180 변환
  // }


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

  
    // =================  롤 제어 =================


    // yaw를 -360~360으로
  float yaw_deg = wrap720_deg(flightData.filterRoll);  // 0~720
  if (yaw_deg > 360.0f) yaw_deg -= 720.0f;  // -360~360 변환
  
  float diff = yaw_deg - prev_yaw;
  if (diff > 180.0f) yaw_deg -= 360.0f;    // 179° → -179°일 때 -181° → 181°로
  else if (diff < -180.0f) yaw_deg += 360.0f;  // 반대 경우 +360°

  prev_yaw = yaw_deg; 

 float servoOffset;
 if (yaw_deg <= 0.0f) {
    // -180 ~ 0 → -10 ~ 0
    float servoOffset = fmap(yaw_deg, -360.0f, 0.0f, -MAX_SERVO_LIMIT, 0.0f);
    
  } else {
    // 0 ~ 180 → 0 ~ +10
    float servoOffset = fmap(yaw_deg, 0.0f, 360.0f, 0.0f, MAX_SERVO_LIMIT);
 
  }
  
  // 최종 서보 각도 계산 및 클램프
  float servoDeg1 = SERVO_NEUTRAL_DEG1 + servoOffset;
  float servoDeg2 = SERVO_NEUTRAL_DEG2 + servoOffset;
  
  
  // 안전 범위 제한 
  servoDeg1 = constrain(servoDeg1, SERVO_NEUTRAL_DEG1 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG1 + MAX_SERVO_LIMIT);
  servoDeg2 = constrain(servoDeg2, SERVO_NEUTRAL_DEG2 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG2 + MAX_SERVO_LIMIT);
  

  // 서보 출력
  writeServoDeg(MOTOR_CH1, servoDeg1);
  writeServoDeg(MOTOR_CH2, servoDeg2);
  


  // Serial.print("Yaw: "); 
      //   Serial.print(imuData.ax, 2); Serial.print(F("//"));
      //  Serial.print(imuData.ay, 2);  Serial.print(F("//"));
      //  Serial.print(imuData.az, 2); Serial.println(F("//"));


//     Serial.print(0.5); Serial.print(",");
//    Serial.print(-0.5); Serial.print(",");
   Serial.print(",");
     Serial.print(flightData.filterRoll, 6);
// Serial.print(",");
//   Serial.print(flightData.pitch, 6);
// Serial.print(",");
//   Serial.print(flightData.yaw, 6);

  // Serial.println(flightData.filterRoll, 6);
 // Serial.print(" Servo1: "); Serial.print(servoDeg1, 1);
 // Serial.print(" Servo2: "); Serial.println(servoDeg2, 1);

  }
  
  // 센서, 통신, 낙하산보드로 데이터 전송
  flightData.timeMs = millis();
  static uint32_t lastTx = 0;
  uint32_t now = millis();
  if (now - lastTx >= 10) {
    lastTx += 10;
    sendAtoB();
  
  }


}