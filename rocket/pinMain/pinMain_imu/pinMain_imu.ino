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

// ===== 속도 (3축 -> 지구고정좌표계에서 적분) =====
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
static const uint8_t  MOTOR_CH1   = 14;
static const uint8_t  MOTOR_CH2   = 15;

static const uint16_t SERVO_MIN_US = 500;
static const uint16_t SERVO_MAX_US = 2500;

static const float   SERVO_NEUTRAL_DEG1 = 79.0f;  //b급은 79.0
static const float   SERVO_NEUTRAL_DEG2 = 79.0f;  //b급은 79.0
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
static const uint8_t LEN   = 24;

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
  push_i16_le(buf, idx, s16_scale(flightData.roll,       32767.0f));
  push_i16_le(buf, idx, s16_scale(flightData.filterRoll, 100.0f));
  push_i16_le(buf, idx, s16_scale(flightData.pitch,      32767.0f));
  push_i16_le(buf, idx, s16_scale(flightData.yaw,        32767.0f));

  push_i16_le(buf, idx, s16_scale(flightData.veltotal, 1.0f));
  push_i16_le(buf, idx, s16_scale(flightData.velimu,   1.0f));

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

    // mahony0 객체가 선언되어 있고 update가 완료된 상태여야 함
    mahony0.getGravityVector(&gravityX, &gravityY, &gravityZ);

    // ================= 변수 선언부 =================
    const float GRAVITY = 980.665f;

    const float P_DEADZONE_DEG = 1.5f;
    const float V_CONTROL_START = 500.0f;
    const float V_CONTROL_FULL  = 2000.0f;

    // [축방향 가속도 데드존] 단위 = cm/s^2 (선가속과 동일). 3축 각각에 동일 적용.
    //   캘리브레이션(bias_b*) 보정 후에도 남는 미세 바이어스/노이즈를
    //   적분 단계에서 한 번 더 죽여 coast 구간 속도 드리프트를 억제.
    //   30.0f = 0.3 m/s^2. 0.0f 로 두면 데드존 끄고 캘리브레이션만 사용.
    const float ACCEL_DEADZONE = 30.0f;

    static float filtered_gyro_z = 0.0f;
    const float GYRO_LPF_ALPHA = 0.2f;

    // ================= 3축 속도 추정 (지구고정좌표계 적분) =================
    //  바디프레임 x/y 는 롤로 계속 회전 -> 그대로 적분하면 무의미(원래 축방향만 쓴 이유).
    //  대신 '중력 제거한 바디프레임 선가속'을 자세 쿼터니언으로 '지구고정좌표계'로
    //  회전시킨 뒤, 회전하지 않는 그 좌표계에서 3축을 각각 적분한다. (정상 strapdown)
    //  ※ 가정: flightData.imu.ax/ay/az 가 az 와 동일하게 cm/s^2, 바디프레임.

    // 1. 중력 제거한 바디프레임 선가속 (3축)
    float lin_bx = flightData.imu.ax - GRAVITY * gravityX;
    float lin_by = flightData.imu.ay - GRAVITY * gravityY;
    float lin_bz = flightData.imu.az - GRAVITY * gravityZ;

    // 2. 현재 자세 쿼터니언 (getGravityVector 와 동일 규약: q=[w,x,y,z])
    float qw, qx, qy, qz;
    mahony0.getQuaternion(&qw, &qx, &qy, &qz);

    unsigned long currentTimeVel = micros();

    // [최초 실행 가드] prevTimeVel=0 으로 인한 첫 루프 dt 폭주 방지.
    static bool velTimeInit = false;
    if (!velTimeInit) {
      prevTimeVel = currentTimeVel;
      velTimeInit = true;
    }

    bool launched = (digitalRead(PIN_CONNECT_DETECT) == HIGH);

    // [발사 상승엣지] 발사 순간 3축 속도 0에서 시작 + dt 기준 리셋.
    static bool prevLaunched = false;
    if (launched && !prevLaunched) {
      vel_ex = vel_ey = vel_ez = 0.0f;
      prevTimeVel = currentTimeVel;      // 첫 적분 구간을 한 루프로 제한 (dtt=0)
    }
    prevLaunched = launched;

    float dtt = (currentTimeVel - prevTimeVel) / 1000000.0f;
    prevTimeVel = currentTimeVel;

    // 디버그용: 지구좌표계 선가속 (바이어스/데드존/회전 후, cm/s^2)
    float aex = 0.0f, aey = 0.0f, aez = 0.0f;

    // 3. 발사 전 바이어스(바디프레임 3축) 캘리브레이션 + 발사 후 (보정→데드존→회전→적분)
    //    ※ ZUPT(정지판정 속도0)는 정지가 보장된 '패드'에서만.
    //      가속도+자이로로는 '정지'와 '등속운동'을 구분 못 하므로 비행 중엔 안 건다.
    if (!launched) {
      // [패드 정지] 바디프레임 선가속 잔여값 = 바이어스. 3축 각각 평균.
      //   정지가 물리적으로 보장되는 유일한 구간 -> 여기서만 속도 0 (패드 ZUPT).
      bias_acc_x += lin_bx; bias_acc_y += lin_by; bias_acc_z += lin_bz;
      bias_n++;
      bias_bx = bias_acc_x / (float)bias_n;
      bias_by = bias_acc_y / (float)bias_n;
      bias_bz = bias_acc_z / (float)bias_n;
      vel_ex = vel_ey = vel_ez = 0.0f;
    } else {
      // 3-1. 바디프레임에서 바이어스 보정 + 데드존 (바이어스는 바디 고정이라 여기서 처리)
      float bx = lin_bx - bias_bx;
      float by = lin_by - bias_by;
      float bz = lin_bz - bias_bz;
      if (fabs(bx) < ACCEL_DEADZONE) bx = 0.0f;
      if (fabs(by) < ACCEL_DEADZONE) by = 0.0f;
      if (fabs(bz) < ACCEL_DEADZONE) bz = 0.0f;

      // 3-2. 바디프레임 -> 지구고정좌표계 회전 (C_b^n)
      //   [ q0²+q1²-q2²-q3²   2(q1q2-q0q3)     2(q1q3+q0q2) ]
      //   [ 2(q1q2+q0q3)      q0²-q1²+q2²-q3²  2(q2q3-q0q1) ]
      //   [ 2(q1q3-q0q2)      2(q0q1+q2q3)     q0²-q1²-q2²+q3² ]
      aex = (qw*qw+qx*qx-qy*qy-qz*qz)*bx + 2.0f*(qx*qy-qw*qz)*by       + 2.0f*(qx*qz+qw*qy)*bz;
      aey = 2.0f*(qx*qy+qw*qz)*bx       + (qw*qw-qx*qx+qy*qy-qz*qz)*by + 2.0f*(qy*qz-qw*qx)*bz;
      aez = 2.0f*(qx*qz-qw*qy)*bx       + 2.0f*(qw*qx+qy*qz)*by       + (qw*qw-qx*qx-qy*qy+qz*qz)*bz;

      // 3-3. 지구고정좌표계에서 3축 적분 (축이 회전 안 하므로 적분이 유효)
      vel_ex += aex * dtt;
      vel_ey += aey * dtt;
      vel_ez += aez * dtt;
    }

    // 4. 3축 속도 벡터 크기 = 로켓 속력
    float total_speed = sqrt(vel_ex*vel_ex + vel_ey*vel_ey + vel_ez*vel_ez);
    flightData.veltotal = total_speed;
    flightData.velimu = total_speed;

    // ================= 롤 제어 =================
    if (dt > 0.0f) {

      // 1. 센서 각도 랩핑(Wrapping) 및 180도 경계선 스파이크 방지
      float yaw_deg = wrap720_deg(flightData.filterRoll);  // 0~720
      if (yaw_deg > 360.0f) yaw_deg -= 720.0f;  // -360~360 변환

      // 이전 각도와의 차이를 비교하여 180도 / -180도 경계선 점프 현상 보정
      float diff = yaw_deg - prev_yaw;
      if (diff > 180.0f) yaw_deg -= 360.0f;        // 179° → -179°로 튈 때 부드럽게 이어줌
      else if (diff < -180.0f) yaw_deg += 360.0f;  // 반대 경우 보정

      prev_yaw = yaw_deg; // 다음 루프를 위해 저장

      // -----------------------------------------------------------------
      // 2. 동적 게인(Gain Scheduling) 기반 PD 제어 로직
      // -----------------------------------------------------------------
      // 2-1. 수직 비행을 위한 목표 각도 (0도)
      float targetYaw = 0.0f;

      // 2-2. 현재 각도와의 오차(Error) 계산 (랩핑 처리된 yaw_deg 사용)
      float errorYaw = targetYaw - yaw_deg;

      // 3. PD 제어항 분리 처리 (민감도 해결 로직 유지)
      float p_error = errorYaw;
      if (abs(p_error) < P_DEADZONE_DEG) p_error = 0.0f; // Deadzone

      // D항: 각속도 기반 댐핑 + LPF
      float current_gz = flightData.imu.gz;
      filtered_gyro_z = (GYRO_LPF_ALPHA * current_gz) + (1.0f - GYRO_LPF_ALPHA) * filtered_gyro_z;

      // 4. 제어 권한(Authority) 계산 - 3축 속력 기반
      float authority = (total_speed - V_CONTROL_START) / (V_CONTROL_FULL - V_CONTROL_START);
      authority = constrain(authority, 0.0f, 1.0f);

      // 5. 게인 적용 및 최종 출력
      float kp = 0.0f, kd = 0.0f;
      getKPKD(total_speed, kp, kd);

      float p_term = kp * p_error;
      float d_term = kd * (-filtered_gyro_z);

      outputYaw = -(p_term + d_term) * authority;

      // -----------------------------------------------------------------
      // 6. 서보 모터 각도 적용 및 출력
      // -----------------------------------------------------------------
      // 서보 모터 각도 적용 (서보 중립 + PD 제어량)
      // ※ 실제 핀 구조에 따라 outputYaw의 부호(+/-)를 반대로 해야 할 수 있습니다.
      servoDeg1 = SERVO_NEUTRAL_DEG1 + outputYaw;
      servoDeg2 = SERVO_NEUTRAL_DEG2 + outputYaw;

      // 서보 기구부 및 핀 보호를 위한 한계치 제한 (Clamping)
      servoDeg1 = constrain(servoDeg1, SERVO_NEUTRAL_DEG1 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG1 + MAX_SERVO_LIMIT);
      servoDeg2 = constrain(servoDeg2, SERVO_NEUTRAL_DEG2 - MAX_SERVO_LIMIT, SERVO_NEUTRAL_DEG2 + MAX_SERVO_LIMIT);

      // 서보 모터 물리적 출력
      writeServoDeg(MOTOR_CH1, servoDeg1);
      writeServoDeg(MOTOR_CH2, servoDeg2);

      // 다음 루프 미분항(D) 연산을 위해 현재 오차 저장
      prevErrorYaw = errorYaw;

      // ================= 디버그 출력 (3축 속도 + 지구 Z 가속도) =================
      Serial.print("Vel:");   Serial.print(total_speed / 100, 2);  // m/s (3축 벡터 크기)
      Serial.print("\tvx:");  Serial.print(vel_ex / 100, 2);       // m/s (지구 X)
      Serial.print("\tvy:");  Serial.print(vel_ey / 100, 2);       // m/s (지구 Y)
      Serial.print("\tvz:");  Serial.print(vel_ez / 100, 2);       // m/s (지구 Z 수직)
      Serial.print("\taez:"); Serial.println(aez, 2);              // 지구 Z 선가속 (cm/s^2)
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
