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
static float outputYaw = 0.0f;
static float yaw_lowpass = 0.0f;
static float drift_estimate = 0.0f;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           
static uint32_t stable_count = 0;
static uint32_t last_stable_time = 0;
  static float prevErrorYaw = 0.0f;
  static unsigned long prevTimePD = 0;
float vel_x = 0.0f;
float vel_y = 0.0f;
float vel_z = 0.0f;
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
static float servoDeg1 = SERVO_NEUTRAL_DEG1; 
static float servoDeg2 = SERVO_NEUTRAL_DEG2;
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
  unsigned long currentTime = micros();
  float dt = (currentTime - prevTimePD) / 1000000.0f;
  prevTimePD = currentTime;

// [변수 선언부 업데이트]
const float GRAVITY = 980.665f; 
const float VEL_DAMPING_MOVING = 1.0f; // 움직임 중: 거의 1.0에 가깝게 유지 (물리값 보존)
const float VEL_DAMPING_STILL = 1.0f;    // 정지 중: 빠르게 드리프트 제거
const float STATIONARY_THRESHOLD = 1200.2f; // 정지 판단 임계값 (노이즈 수준에 따라 조정)

// ... loop() 내부 ...

// 1. 순수 선가속도 추출 (중력 보정)
float pure_ax = ax_f; 
float pure_ay = ay_f;
float pure_az = az_f - GRAVITY; // Z축 중력 제거

// 2. 가속도 벡터 크기 계산 (정지 판별용)
float accel_mag = sqrt(pure_ax * pure_ax + pure_ay * pure_ay + pure_az * pure_az);

// 3. 조건부 속도 적분 및 적응형 댐핑
if (accel_mag < STATIONARY_THRESHOLD) {
    // [정지 상태] 가속도가 작으면 센서 드리프트로 간주하고 속도를 0으로 강제 수렴
    vel_x *= VEL_DAMPING_STILL; 
    vel_y *= VEL_DAMPING_STILL;
    vel_z *= VEL_DAMPING_STILL;
    
    // 임계값 이하 시 완전 정지
    if (abs(vel_x) < 0.005f) vel_x = 0.0f;
    if (abs(vel_y) < 0.005f) vel_y = 0.0f;
    if (abs(vel_z) < 0.005f) vel_z = 0.0f;
} 
else {
    // [이동 상태] 실제 가속도가 감지되면 댐핑을 거의 하지 않고 물리값에 가깝게 적분
    vel_x += pure_ax * dt;
    vel_y += pure_ay * dt;
    vel_z += pure_az * dt;
    
    // 수치적 발산만 간신히 막는 수준의 초약세 댐핑
    vel_x *= VEL_DAMPING_MOVING;
    vel_y *= VEL_DAMPING_MOVING;
    vel_z *= VEL_DAMPING_MOVING;
}

// 4. 속도 결과 필터링 (필요 시 추가적인 LPF 적용 가능)
float total_speed = sqrt(vel_x * vel_x + vel_y * vel_y + vel_z * vel_z);


if (dt > 0.0f) {
    // -----------------------------------------------------------------
    // 1. 센서 각도 랩핑(Wrapping) 및 180도 경계선 스파이크 방지
    // -----------------------------------------------------------------
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
    // 2-1. 현재 속도(vel)를 기반으로 동적 Kp, Kd 값 갱신
    float kp = 0.0f;
    float kd = 0.0f;
// loop() 내부 PD 제어 직전 추가
float control_velocity = total_speed;



    // 2-2. 수직 비행을 위한 목표 각도 (0도)
    float targetYaw = 0.0f;

    // 2-3. 현재 각도와의 오차(Error) 계산
    // [중요] 필터에서 바로 나온 값이 아닌, 랩핑 처리가 완료된 yaw_deg를 사용합니다.
    float errorYaw = targetYaw - yaw_deg;       

    // 2-4. 오차의 변화율(Derivative) 계산
    float dErrorYaw = (errorYaw - prevErrorYaw) / dt;
// 속도가 너무 낮으면 노이즈로 간주하고 제어를 감쇠하거나 정지
if (control_velocity < 2000.0f) { // 단위가 m/s라면 0.5m/s 이하
    control_velocity = 0.0f;
    outputYaw = 0.0f; // 제어 출력 초기화
} else {
    getKPKD(control_velocity/100.0f, kp, kd); // 임계값 이상일 때만 게인 계산
    outputYaw = (kp * errorYaw) + (kd * dErrorYaw);
}
   

    // -----------------------------------------------------------------
    // 3. 서보 모터 각도 적용 및 출력
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

          Serial.print(servoDeg1); Serial.print(F("//"));
       Serial.print(servoDeg2);  Serial.print(F("//"));
       Serial.println(total_speed, 2); 
  }
  


  // Serial.print("Yaw: "); 
  

//     Serial.print(0.5); Serial.print(",");
//    Serial.print(-0.5); Serial.print(",");
  //  Serial.print(",");
  //    Serial.print(flightData.filterRoll, 6);
// Serial.print(",");
//   Serial.print(flightData.pitch, 6);
// Serial.print(",");
//   Serial.print(flightData.yaw, 6);

  // Serial.println(flightData.filterRoll, 6);
//  Serial.print(imuData.ax);Serial.print(","); Serial.print(imuData.az);Serial.print(",");
//  Serial.println(imuData.ay);// Serial.println(servoDeg2, 1);

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