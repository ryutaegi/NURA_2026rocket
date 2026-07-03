#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <float.h>
#include <avr/wdt.h>
#include "Adafruit_AHRS_Mahony.h"
#include "pin.h"           // 상보필터/IMU 처리
#include "servo_driver.h"

#define PIN_CONNECT_DETECT 2
Adafruit_Mahony mahony0;

//누적값
static unsigned long prevTimeVel = 0;
float yaw_drift_total = 0.00f;
static float outputYaw = 0.0f;
static float yaw_lowpass = 0.0f;
static float drift_estimate = 0.0f;
static uint32_t stable_count = 0;
static uint32_t last_stable_time = 0;
static float prevErrorYaw = 0.0f;
static unsigned long prevTimePD = 0;

// ===== 속도 및 센서 융합 변수 =====
float vel_ex = 0.0f;   // 지구 X 속도 (cm/s)
float vel_ey = 0.0f;   // 지구 Y 속도 (cm/s)
float vel_ez = 0.0f;   // 지구 Z(수직) 속도 (cm/s)


// ===== 발사 전 '바디프레임' 선가속 바이어스 추정용 (3축) =====
//   가속도 바이어스는 센서(바디)에 고정 -> 반드시 바디프레임에서 보정 후 회전.
static float    bias_bx    = 0.0f, bias_by = 0.0f, bias_bz = 0.0f;
static float    bias_acc_x = 0.0f, bias_acc_y = 0.0f, bias_acc_z = 0.0f;
static uint32_t bias_n     = 0;

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

static const float   SERVO_NEUTRAL_DEG1 = 89.5f;  //흰
static const float   SERVO_NEUTRAL_DEG2 = 76.5f;  //검
static float servoDeg1 = SERVO_NEUTRAL_DEG1;
static float servoDeg2 = SERVO_NEUTRAL_DEG2;
// [설정] 서보 물리적 제한 각도
static const float    MAX_SERVO_LIMIT = 18.0f;

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
  push_i16_le(buf, idx, flightData.imu.ax);
  push_i16_le(buf, idx, flightData.imu.ay);
  push_i16_le(buf, idx, flightData.imu.az);
  
  // gyro: deg/s * 10
  push_i16_le(buf, idx, s16_scale(flightData.imu.gx, 10.0f));
  push_i16_le(buf, idx, s16_scale(flightData.imu.gy, 10.0f));
  push_i16_le(buf, idx, s16_scale(flightData.imu.gz, 10.0f));

  // angles: deg * 100
  push_i16_le(buf, idx, s16_scale(flightData.roll,       32767.0f));
  push_i16_le(buf, idx, s16_scale(flightData.filterRoll, 100.0f));
  push_i16_le(buf, idx, s16_scale(flightData.pitch,      32767.0f));
  push_i16_le(buf, idx, s16_scale(flightData.yaw,        32767.0f));

  // CRC over [VER..PAYLOAD]
  uint16_t crc = crc16_ccitt(&buf[2], (size_t)(idx - 2));
  push_u16_le(buf, idx, crc);

  Serial3.write(buf, idx);
}

void softwareReset() {
  wdt_enable(WDTO_15MS);  // 15ms 후 리셋
  while (1) {}            // 대기 → WDT 트리거
}


static const uint8_t B2A_SYNC1 = 0xB5;
static const uint8_t B2A_SYNC2 = 0x5B;
static const uint8_t B2A_VER   = 1;

// B보드 메시지 종류
static const uint8_t B2A_MSG_RESET     = 0x31;  // 원격 리셋
static const uint8_t B2A_MSG_CLIMBRATE = 0x32;  // 기압 기반 상승률
uint32_t B2A_rxByteCount = 0;
uint32_t B2A_crcFailCount = 0;
uint32_t B2A_climbPacketCount = 0;
uint32_t B2A_resetPacketCount = 0;
static const uint8_t B2A_MAX_LEN = 16;


// ============================================================
// B보드에서 수신한 기압 기반 상승률
// ============================================================

// 단위: m/s
// 상승 중 +, 하강 중 -
float ClimbRate = 0.0f;

// 정상 상승률 패킷을 마지막으로 받은 시각
uint32_t ClimbRateRxMs = 0;

// B보드가 패킷에 넣어 보낸 시간
uint32_t ClimbRateTimeMs = 0;

// 상승률 패킷 정상 수신 여부
bool ClimbRateValid = false;


// ============================================================
// Little-endian 읽기 함수
// ============================================================

static inline uint16_t rd_u16_le(const uint8_t* p) {
  return (uint16_t)p[0]
       | ((uint16_t)p[1] << 8);
}

static inline int16_t rd_i16_le(const uint8_t* p) {
  return (int16_t)rd_u16_le(p);
}

static inline uint32_t rd_u32_le(const uint8_t* p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

void pollB2A(Stream& link, uint32_t nowMs) {
  enum ParseState {
    WAIT_SYNC1,
    WAIT_SYNC2,
    READ_HEADER,
    READ_PAYLOAD,
    READ_CRC
  };

  static ParseState state = WAIT_SYNC1;

  static uint8_t hdr[5];
  static uint8_t payload[B2A_MAX_LEN];
  static uint8_t crcBytes[2];

  static uint8_t hdrIdx = 0;
  static uint8_t payloadIdx = 0;
  static uint8_t payloadLen = 0;
  static uint8_t crcIdx = 0;

  while (link.available()) {
    uint8_t b = (uint8_t)link.read();
    B2A_rxByteCount++;

    switch (state) {

      case WAIT_SYNC1:
        if (b == B2A_SYNC1) {
          state = WAIT_SYNC2;
        }
        break;

      case WAIT_SYNC2:
        if (b == B2A_SYNC2) {
          hdrIdx = 0;
          state = READ_HEADER;
        } else {
          state = WAIT_SYNC1;
        }
        break;

      case READ_HEADER:
        hdr[hdrIdx++] = b;

        if (hdrIdx >= 5) {
          uint8_t version = hdr[0];
          uint8_t len = hdr[2];

          if (version != B2A_VER || len > B2A_MAX_LEN) {
            state = WAIT_SYNC1;
            break;
          }

          payloadLen = len;
          payloadIdx = 0;
          crcIdx = 0;

          state = (payloadLen == 0) ? READ_CRC : READ_PAYLOAD;
        }
        break;

      case READ_PAYLOAD:
        payload[payloadIdx++] = b;

        if (payloadIdx >= payloadLen) {
          crcIdx = 0;
          state = READ_CRC;
        }
        break;

      case READ_CRC:
        crcBytes[crcIdx++] = b;

        if (crcIdx >= 2) {
          uint8_t crcBuf[5 + B2A_MAX_LEN];

          memcpy(crcBuf, hdr, 5);
          memcpy(crcBuf + 5, payload, payloadLen);

          uint16_t crcCalc = crc16_ccitt(crcBuf, 5 + payloadLen);
          uint16_t crcRecv = rd_u16_le(crcBytes);

          if (crcCalc == crcRecv) {
            uint8_t msg = hdr[1];

            // ------------------------------------------------
            // 0x31: 기존 원격 리셋 명령
            // ------------------------------------------------
            if (msg == B2A_MSG_RESET) {
            if (payloadLen >= 1 && payload[0] == 1) {
            softwareReset();
         }
        }

            // ------------------------------------------------
            // 0x32: 상승률 수신
            //
            // payload[0..1] = ClimbRate × 100, int16_t
            // payload[2..5] = B보드 timestamp, uint32_t
            // ------------------------------------------------
            else if (msg == B2A_MSG_CLIMBRATE && payloadLen == 6) {
              B2A_climbPacketCount++;
              int16_t climbRateX100 = rd_i16_le(&payload[0]);

              // 예: 1234 → 12.34 m/s
              ClimbRate = climbRateX100 / 100.0f;

              ClimbRateTimeMs = rd_u32_le(&payload[2]);
              ClimbRateRxMs = nowMs;
              ClimbRateValid = true;
            }
          }
          else {
  B2A_crcFailCount++;
}
          

          state = WAIT_SYNC1;
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

  pinMode(PIN_CONNECT_DETECT, INPUT_PULLUP);
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

  // 분리한 설정 함수 호출
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
}

void loop() {
  pollB2A(Serial3, millis());
  // ================= IMU 자동 복구 =================

  // 데이터 읽기 시도
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
  }

  // 센서가 비정상일 때 복구 시도
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
  if (dataAvailable && isImuHealthy) {

    processIMU();  // 상보필터 업데이트

    bool isSpike = (abs(myICM.accX()) > ACCEL_AXIS_LIMIT) || (abs(myICM.accY()) > ACCEL_AXIS_LIMIT) || (abs(myICM.accZ()) > ACCEL_AXIS_LIMIT);

    if (isSpike) {
      spikeCounter++;

      if (spikeCounter >= MAX_SPIKE_COUNT) {
        // [n회 이상 연속] -> 센서 고장으로 판단
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

    unsigned long currentTime = micros();
    float dt = (currentTime - prevTimePD) / 1000000.0f;
    prevTimePD = currentTime;

    float gravityX, gravityY, gravityZ;
    mahony0.updateIMU(gx_f, gy_f, gz_f, ax_f, ay_f, az_f, dt);
    mahony0.computeAngles();

    mahony0.getGravityVector(&gravityX, &gravityY, &gravityZ);

    // ================= 변수 선언부 =================
    const float GRAVITY = 980.665f;

    const float P_DEADZONE_DEG = 1.5f;
    const float V_CONTROL_START = 500.0f;
    const float V_CONTROL_FULL  = 2000.0f;

    static float filtered_gyro_z = 0.0f;
    const float GYRO_LPF_ALPHA = 0.2f;

    // ================= 3축 속도 추정 (지구고정좌표계 적분) =================
    // 1. 중력 제거한 바디프레임 선가속 (3축)
    float lin_bx = flightData.imu.ax - GRAVITY * gravityX;
    float lin_by = flightData.imu.ay - GRAVITY * gravityY;
    float lin_bz = flightData.imu.az - GRAVITY * gravityZ;

    // 2. 현재 자세 쿼터니언
    float qw, qx, qy, qz;
    mahony0.getQuaternion(&qw, &qx, &qy, &qz);

    unsigned long currentTimeVel = micros();

    static bool velTimeInit = false;
    if (!velTimeInit) {
      prevTimeVel = currentTimeVel;
      velTimeInit = true;
    }

    bool launched = (digitalRead(PIN_CONNECT_DETECT) == HIGH);

    static bool prevLaunched = false;
    if (launched && !prevLaunched) {
      vel_ex = vel_ey = vel_ez = 0.0f;
      prevTimeVel = currentTimeVel;      
    }
    prevLaunched = launched;

    float dtt = (currentTimeVel - prevTimeVel) / 1000000.0f;
    prevTimeVel = currentTimeVel;

    float aex = 0.0f, aey = 0.0f, aez = 0.0f;

    // 3. 발사 전 바이어스 캘리브레이션 + 발사 후 속도 융합(상보필터)
    if (!launched) {
      // 패드 정지 시 바디프레임 선가속 잔여값을 바이어스로 산출
      bias_acc_x += lin_bx; bias_acc_y += lin_by; bias_acc_z += lin_bz;
      bias_n++;
      bias_bx = bias_acc_x / (float)bias_n;
      bias_by = bias_acc_y / (float)bias_n;
      bias_bz = bias_acc_z / (float)bias_n;
      vel_ex = vel_ey = vel_ez = 0.0f;
    } else {
      // 3-1. 바디프레임 바이어스 보정 (데드존 삭제)
      float bx = lin_bx - bias_bx;
      float by = lin_by - bias_by;
      float bz = lin_bz - bias_bz;

      // 3-2. 지구고정좌표계로 회전 변환
      aex = (qw*qw+qx*qx-qy*qy-qz*qz)*bx + 2.0f*(qx*qy-qw*qz)*by       + 2.0f*(qx*qz+qw*qy)*bz;
      aey = 2.0f*(qx*qy+qw*qz)*bx       + (qw*qw-qx*qx+qy*qy-qz*qz)*by + 2.0f*(qy*qz-qw*qx)*bz;
      aez = 2.0f*(qx*qz-qw*qy)*bx       + 2.0f*(qw*qx+qy*qz)*by       + (qw*qw-qx*qx-qy*qy+qz*qz)*bz;

      // 3-3. 지구고정좌표계 속도 산출
      // X, Y축: 기준 데이터가 없으므로 가속도 단순 적분 (수평 방향 표류 오차 주의)
      vel_ex += aex * dtt;
      vel_ey += aey * dtt;

      // Z축: IMU 가속도와 기압계(BMP280) 데이터 상보 필터 융합
      const float ALPHA_Z = 0.98f; // 필터 계수 (필요시 0.95 ~ 0.99 튜닝)
      float baro_vz = ClimbRate * 100.0f; // m/s를 cm/s로 단위 통일

      vel_ez = ALPHA_Z * (vel_ez + aez * dtt) + (1.0f - ALPHA_Z) * baro_vz;
    }

    // 4. 3축 속도 벡터 크기
    float total_speed = sqrt(vel_ex*vel_ex + vel_ey*vel_ey + vel_ez*vel_ez);

    // ================= 롤 제어 =================
    if (dt > 0.0f) {
      float yaw_deg = wrap720_deg(flightData.filterRoll);  
      if (yaw_deg > 360.0f) yaw_deg -= 720.0f;  

      float diff = yaw_deg - prev_yaw;
      if (diff > 180.0f) yaw_deg -= 360.0f;        
      else if (diff < -180.0f) yaw_deg += 360.0f;  

      prev_yaw = yaw_deg; 

      float targetYaw = 0.0f;
      float errorYaw = targetYaw - yaw_deg;

      float p_error = errorYaw;
      if (abs(p_error) < P_DEADZONE_DEG) p_error = 0.0f; 

      float current_gz = flightData.imu.gz;
      filtered_gyro_z = (GYRO_LPF_ALPHA * current_gz) + (1.0f - GYRO_LPF_ALPHA) * filtered_gyro_z;

      float authority = (total_speed - V_CONTROL_START) / (V_CONTROL_FULL - V_CONTROL_START);
      authority = constrain(authority, 0.0f, 1.0f);

      float kp = 0.0f, kd = 0.0f;
      getKPKD(total_speed, kp, kd);

      float p_term = kp * p_error;
      float d_term = kd * (-filtered_gyro_z);

      outputYaw = -(p_term + d_term) * authority;

      servoDeg1 = SERVO_NEUTRAL_DEG1 + outputYaw;
      servoDeg2 = SERVO_NEUTRAL_DEG2 + outputYaw;

      servoDeg1 = constrain(servoDeg1, SERVO_NEUTRAL_DEG1 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG1 + MAX_SERVO_LIMIT);
      servoDeg2 = constrain(servoDeg2, SERVO_NEUTRAL_DEG2 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG2 + MAX_SERVO_LIMIT);

      writeServoDeg(MOTOR_CH1, servoDeg1);
      writeServoDeg(MOTOR_CH2, servoDeg2);

      prevErrorYaw = errorYaw;

      // ================= 디버그 출력 =================
      Serial.print("Vel:");   Serial.print(total_speed / 100, 2);  
      Serial.print("\tvx:");  Serial.print(vel_ex / 100, 2);       
      Serial.print("\tvy:");  Serial.print(vel_ey / 100, 2);       
      Serial.print("\tvz:");  Serial.print(vel_ez / 100, 2);       
      Serial.print("\taez:"); Serial.print(aez, 2);       
      Serial.print("ClimbRate:"); Serial.println(ClimbRate, 2);          
    }
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