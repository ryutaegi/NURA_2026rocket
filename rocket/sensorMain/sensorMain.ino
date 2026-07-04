#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SdFat.h>
#include <EEPROM.h>
#include <avr/wdt.h>

#include "lora.h"
#include "parachute.h"
#include "flightType.h"
#include "debug.h" 
/* 이걸로 디버깅하면 됨
예시)
debugln("System start");

debugln("Init GPS");
debugln("Init BMP280");
debugln("Init SD");

debugVar(altitude);
debugVar(speed);
*/



#define PIN_CONNECT_DETECT 2
#define PIN_DEPLOY_SERVO 6
#define PIN_BUZZER 7

static const int SD_CS_PIN = 10;
const int EEPROM_ADDR_IDX = 0;          // EEPROM에 uint16_t 인덱스 저장 주소
const uint32_t LOG_PERIOD_MS = 100;      // 10Hz
const uint32_t FLUSH_PERIOD_MS = 1000;  // 1초
// SdFat: FAT32/exFAT 모두 대응하기 위해 SdFs + FsFile 사용
SdFs sd;
FsFile logFile;

// ===== BMP 압력 3점 중앙값 필터 =====
static float g_pressHist[3] = {0.0f, 0.0f, 0.0f};
static uint8_t g_pressCount = 0;
static uint8_t g_pressHead = 0;

float median3(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

JudgeCounters jc;
float prevClimbRate = 0;

Servo deployServo;
DeployController deployCtl;

//커넥트핀 연결을 단 한번만 판단하게함
bool pinDetached = false;
bool g_parachuteDeployed = false;  //낙하산 사출 여부
bool isReset = false;  //리셋 전송

// 낙하산 사출 여부 핀 보드로의 송신을 위한 변수 선언
static bool lastParachute = false; 

static bool b2aBurst = false;
static uint32_t b2aBurstStartMs = 0;
static uint32_t b2aLastSendMs = 0;

FlightData flight;
// 1) BMP280
// ============================================================================
Adafruit_BMP280 bmp;

static const uint32_t BARO_PERIOD_MS = 50;  // 20Hz
static uint32_t g_baro_lastMs = 0;          // 마지막으로 updateBaro()가 실제로 센서를 읽은 시간을 저장하는 변수.

// 상대고도 기준압 p0 (발사대에서 평균낸 압력)
static float g_p0_hPa = 1013.25f;  // 기준 압력 p0

// ==================== 고도 / 상승률 필터 설정 ====================

// 고도 LPF:
// 0에 가까울수록 매우 부드럽지만 늦음
// 1에 가까울수록 빠르지만 튐
// 0.55는 새 측정값을 55% 반영하는 비교적 빠른 세팅
static const float ALT_ALPHA = 0.55f;

// 최근 5개 고도값 사용
// BARO_PERIOD_MS = 50ms이면 약 0.20초 구간의 평균 기울기로 속도 계산
static const uint8_t VEL_WINDOW = 5;

static bool g_altInitialized = false;
static float g_altFiltered = 0.0f;

static float g_altHistory[VEL_WINDOW];
static uint32_t g_timeHistory[VEL_WINDOW];

static uint8_t g_histHead = 0;   // 다음에 덮어쓸 위치
static uint8_t g_histCount = 0;  // 현재 저장된 샘플 수

static float g_climbFilt = 0.0f;
static const float CLIMB_ALPHA = 0.60f;

static bool isValidPressure_hPa(float p) {
  return (p >= 300.0f && p <= 1100.0f);
}  // 기압 범위가 300~1100인지 확인

// 패킷 비트 변수
bool ejectBtnClicked = false;        // bit2


uint8_t satCount = flight.gps.sats;       // bit7:4, 0~15

uint8_t launchStage = (uint8_t)flight.state; // bit7:5, 0~7
bool extra1 = false;       // bit4
bool chuteByDescent = false;         // bit3
bool chuteByTimer = false;           // bit2

uint8_t qIndex = 0;                  // bit1:0, 0~3




void softwareReset() {
  wdt_enable(WDTO_15MS);  // 15ms 후 리셋
  while (1) {}            // 대기 → WDT 트리거
}

// ====================⏱️ 발사 시간 측정 변수 ========================
bool launchTimeStarted = false;  // 시간 시작 여부
unsigned long launchTimeMs = 0;  // T0 (발사 시작 시각)

// 표준대기 근사식: p0를 발사대 압력으로 잡으면 상대고도
static float altitudeFromPressure(float p_hPa, float p0_hPa) {
  if (p_hPa <= 0.0f || p0_hPa <= 0.0f) return 0.0f;          // 0이하값은 0으로
  return 43561.54f * (1.0f - powf(p_hPa / p0_hPa, 0.1903f));  // 표준대기근사식으로 고도계산
}

bool initBaro() {
  if (!bmp.begin(0x76)) {
    if (!bmp.begin(0x77)) return false;
  }
  bmp.setSampling(  // BMP280 내부 설정값
    Adafruit_BMP280::MODE_NORMAL,
    Adafruit_BMP280::SAMPLING_X2,
    Adafruit_BMP280::SAMPLING_X8,
    Adafruit_BMP280::FILTER_X2,
    Adafruit_BMP280::STANDBY_MS_1);
  return true;
}

// 부팅 직후 몇 초간 압력 평균, g_p0_hPa 설정 (상대고도 0 기준)
void calibrateBaroP0(uint32_t calibMs = 3000) {
  uint32_t t0 = millis();  // 부팅 후 경과시간
  uint32_t n = 0;          // 샘플개수
  double sum = 0;          // 압력 합

  while (millis() - t0 < calibMs) {
    float p = bmp.readPressure() / 100.0f;  // Pa를 hPa로
    if (isValidPressure_hPa(p)) {
      sum += p;
      n++;
    }
    delay(20);
  }
  if (n > 10) g_p0_hPa = (float)(sum / (double)n);
}

void pushFilteredAltitude(float alt_m, uint32_t nowMs) {
  g_altHistory[g_histHead] = alt_m;
  g_timeHistory[g_histHead] = nowMs;

  g_histHead = (g_histHead + 1) % VEL_WINDOW;

  if (g_histCount < VEL_WINDOW) {
    g_histCount++;
  }
}

float calculateClimbRateFromHistory() {
  if (g_histCount < VEL_WINDOW) return 0.0f;

  uint8_t oldest = g_histHead;
  uint8_t newest = (g_histHead + VEL_WINDOW - 1) % VEL_WINDOW;

  float dh = g_altHistory[newest] - g_altHistory[oldest];
  float dt = (g_timeHistory[newest] - g_timeHistory[oldest]) * 0.001f;

  if (dt < 0.10f || dt > 0.50f) return 0.0f;

  return dh / dt;
}

void updateBaro(FlightData& f, uint32_t nowMs) {
  if (nowMs - g_baro_lastMs < BARO_PERIOD_MS) return;
  g_baro_lastMs = nowMs;

  prevClimbRate = f.baro.climbRate;

  float tempC = bmp.readTemperature();
  float press_hPa = bmp.readPressure() / 100.0f;

  if (!isValidPressure_hPa(press_hPa)) return;

  // 1) 압력 → 원시 상대고도
  float altRaw_m = altitudeFromPressure(press_hPa, g_p0_hPa);

  // 2) 고도 LPF
  if (!g_altInitialized) {
    g_altFiltered = altRaw_m;
    g_altInitialized = true;
  } else {
    g_altFiltered =
      (1.0f - ALT_ALPHA) * g_altFiltered +
      ALT_ALPHA * altRaw_m;
  }

  // 3) 최근 필터 고도값 저장
  pushFilteredAltitude(g_altFiltered, nowMs);

  // 4) 약 0.2초 구간의 고도 변화량으로 상승률 계산
  float climbWindow = calculateClimbRateFromHistory();

  if (g_histCount < VEL_WINDOW) {
  g_climbFilt = 0.0f;
  } else {
  g_climbFilt =
    (1.0f - CLIMB_ALPHA) * g_climbFilt +
    CLIMB_ALPHA * climbWindow;
}

float climb = g_climbFilt;

  // 구조체 저장
  f.baro.temperature = tempC;
  f.baro.pressure = press_hPa;

  // 낙하산 판단과 SD 로그에는 필터된 고도를 사용
  f.baro.altitude = g_altFiltered;
  f.baro.climbRate = climb;

  f.baroTimeMs = nowMs;
}

// 2) GPS
// ============================================================================
TinyGPSPlus gps;

static const uint32_t GPS_PERIOD_MS = 500;  // 구조체 업데이트 주기(5Hz)
static uint32_t g_gps_lastMs = 0;           // 마지막 구조체 반영 시각
static uint32_t g_lastGpsUpdateMs = 0;      // 위/경도 실제 갱신 시각
// 위/경도 정수변환
static int32_t toE7(double deg) {
  double v = deg * 1e7;
  if (v > 2147483647.0) v = 2147483647.0;  // int32_t범위로 제한
  if (v < -2147483648.0) v = -2147483648.0;
  v = (v >= 0.0) ? (v + 0.5) : (v - 0.5);  // 반올림
  return (int32_t)v;
}
uint8_t setNav[] = {
  0xB5,0x62,0x06,0x24,0x24,0x00,
  0xFF,0xFF,0x06,0x03, // Airborne <1g
  0x00,0x00,0x00,0x00,
  0x10,0x27,0x00,0x00,
  0x05,0x00,
  0xFA,0x00,
  0xFA,0x00,
  0x64,0x00,
  0x2C,0x01,
  0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
  0x16,0xDC
};

void initGps() {
  Serial1.begin(9600);
   delay(1000);
  Serial1.write(setNav, sizeof(setNav));
}

// loop에서 가능한 자주 파서에 먹이기
void pollGpsParser() {
  while (Serial1.available()) gps.encode(Serial1.read());  // 수신버퍼에서 1바이트 꺼내서 파서에 먹임
}

// void updateGps(FlightData& f, uint32_t nowMs) {
//   //pollGpsParser();  
//                                    // 계속 파싱해서 구조체 비우기
//   //if (nowMs - g_gps_lastMs < GPS_PERIOD_MS) return;  // 주기유지(200ms)
//   g_gps_lastMs = nowMs;                              // 타임스탬프 갱신

//   // // HDOP
//   //   Serial.print("HDOP: ");
//   //   if (gps.hdop.isValid())
//   //     Serial.println(gps.hdop.hdop());
//   //   else
//   //     Serial.println("N/A");

//   // fix판단
//   bool hasLoc = gps.location.isValid();
//   bool hasFix = hasLoc && (gps.location.age() < 2000);  // 마지막 위치 업데이트 경과시간 2초이내면 fix
//   f.gps.fix = hasFix;
//   // 위성 수 판단
//   f.gps.sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
//   //Serial.println(gps.location.isValid());
//   if (hasLoc) {
//     f.gps.latitudeE7 = toE7(gps.location.lat());
//     f.gps.longitudeE7 = toE7(gps.location.lng());

//     if (gps.location.isUpdated()) flight.gpsTimeMs = nowMs;
//   }

//   if (gps.altitude.isValid()) f.gps.altitude = gps.altitude.meters();  // m
//   if (gps.speed.isValid()) f.gps.speed = gps.speed.mps();              // m/s
//   if (gps.course.isValid()) f.gps.heading = gps.course.deg();          // deg
// }
void updateGps(FlightData& f, uint32_t nowMs) {

  bool hasLoc = gps.location.isValid();
  bool hasFix = hasLoc && (gps.location.age() < 2000);

  f.gps.fix = hasFix;

  f.gps.sats = gps.satellites.isValid() ? 
               (uint8_t)gps.satellites.value() : 0;

  if (hasLoc) {
    f.gps.latitudeE7 = toE7(gps.location.lat());
    f.gps.longitudeE7 = toE7(gps.location.lng());
  }

  if (gps.altitude.isValid())
    f.gps.altitude = gps.altitude.meters();

  if (gps.speed.isValid())
    f.gps.speed = gps.speed.mps();

  if (gps.course.isValid())
    f.gps.heading = gps.course.deg();

  f.gpsTimeMs = nowMs;
}

// ============================================================================
// 3) A2B UART 패킷 수신/파싱
//    IMU + roll/pitch/yaw + filterRoll 갱신
// ============================================================================

// 패킷 구성 (변경 요)
static const uint8_t SYNC1 = 0xA5;
static const uint8_t SYNC2 = 0x5A;
static const uint8_t VER = 1;
static const uint8_t MSG = 0x21;
static const uint8_t LEN = 24;

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

static inline uint16_t rd_u16_le(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline int16_t rd_i16_le(const uint8_t* p) {
  return (int16_t)rd_u16_le(p);
}
static inline void wr_i16_le(uint8_t* p, int16_t v) {
  wr_u16_le(p, (uint16_t)v);
}
static inline uint32_t rd_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ====== Parser: call very often ======
void parseAtoB(Stream& link, FlightData& f, uint32_t nowB_ms) {
  enum { WAIT_S1,
         WAIT_S2,
         READ_HDR,
         READ_BODY } static st = WAIT_S1;

  // Header: VER(1) MSG(1) LEN(1) SEQ(2) TIME(4) = 9
  static uint8_t hdr[9];
  static uint8_t payload[LEN];
  static uint8_t crcBytes[2];

  static uint8_t hdrIdx = 0;
  static uint16_t bodyIdx = 0;

  while (link.available()) {
    uint8_t b = (uint8_t)link.read();

    switch (st) {
      case WAIT_S1:
        if (b == SYNC1) st = WAIT_S2;
        break;

      case WAIT_S2:
        if (b == SYNC2) {
          st = READ_HDR;
          hdrIdx = 0;
        } else st = WAIT_S1;
        break;

      case READ_HDR:
        hdr[hdrIdx++] = b;
        if (hdrIdx >= sizeof(hdr)) {
          uint8_t ver = hdr[0], msg = hdr[1], len = hdr[2];
          if (ver != VER || msg != MSG || len != LEN) {
            st = WAIT_S1;
            break;
          }
          st = READ_BODY;
          bodyIdx = 0;
        }
        break;

      case READ_BODY:
        if (bodyIdx < LEN) {
          payload[bodyIdx++] = b;
        } else if (bodyIdx < (LEN + 2)) {
          crcBytes[bodyIdx - LEN] = b;
          bodyIdx++;
        }

        if (bodyIdx >= (LEN + 2)) {
          // CRC buffer = hdr(9) + payload(LEN)
          uint8_t crcBuf[9 + LEN];
          memcpy(crcBuf, hdr, 9);
          memcpy(crcBuf + 9, payload, LEN);

          uint16_t crcCalc = crc16_ccitt(crcBuf, sizeof(crcBuf));
          uint16_t crcRecv = rd_u16_le(crcBytes);

          if (crcCalc == crcRecv) {
            uint32_t timeA_ms = rd_u32_le(&hdr[5]);
            f.aTimeMs = timeA_ms;
            f.aRxTimeMs = nowB_ms;

            int idx = 0;

            // accel: (m/s^2*10) -> m/s^2
            int16_t ax10 = rd_i16_le(&payload[idx]);
            idx += 2;
            int16_t ay10 = rd_i16_le(&payload[idx]);
            idx += 2;
            int16_t az10 = rd_i16_le(&payload[idx]);
            idx += 2;

            // gyro: (deg/s*10) -> deg/s
            int16_t gx10 = rd_i16_le(&payload[idx]);
            idx += 2;
            int16_t gy10 = rd_i16_le(&payload[idx]);
            idx += 2;
            int16_t gz10 = rd_i16_le(&payload[idx]);
            idx += 2;

            // angles: (deg*100) -> deg
            int16_t roll100 = rd_i16_le(&payload[idx]);
            idx += 2;
            int16_t froll100 = rd_i16_le(&payload[idx]);
            idx += 2;
            int16_t pitch100 = rd_i16_le(&payload[idx]);
            idx += 2;
            int16_t yaw100 = rd_i16_le(&payload[idx]);
            idx += 2;
            // vertical velocity: cm/s 그대로 수신
            int16_t veltotal_cmps = rd_i16_le(&payload[idx]);
            idx += 2;

            int16_t velimu_cmps = rd_i16_le(&payload[idx]);
            idx += 2;

            f.imu.ax = ax10 / 100.0f;
            f.imu.ay = ay10 / 100.0f;
            f.imu.az = az10 / 100.0f;

            f.imu.gx = gx10 / 10.0f;
            f.imu.gy = gy10 / 10.0f;
            f.imu.gz = gz10 / 10.0f;
          
            f.roll = roll100 / 32767.0f;
            f.filterRoll = froll100 / 100.0f;
            f.pitch = pitch100 / 32767.0f;
            f.yaw = yaw100 / 32767.0f;
            f.veltotal = (float)veltotal_cmps;
            f.velimu   = (float)velimu_cmps;
          }

          st = WAIT_S1;
        }
        break;
    }
  }
}

// ============================================================================
// 4) B2A UART 패킷 송신 (g_parachuteDeployed 전송)
//    - Serial3로 핀보드(A보드)에 상태 전달
//    - 프레임 + CRC16 CCITT-FALSE 사용(기존 crc16_ccitt 재사용)
// ============================================================================

static const uint8_t B2A_SYNC1 = 0xB5;
static const uint8_t B2A_SYNC2 = 0x5B;
static const uint8_t B2A_VER   = 1;
static const uint8_t B2A_MSG_RESET     = 0x31;  // 기존 원격 리셋
static const uint8_t B2A_MSG_CLIMBRATE = 0x32;
static const uint8_t B2A_RESET_LEN = 6;
static const uint8_t B2A_CLIMB_LEN = 6;

static uint32_t g_lastClimbTxMs = 0;
static const uint32_t CLIMB_TX_PERIOD_MS = 50;  // 20 Hz

static inline void wr_u16_le(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void wr_u32_le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void sendBtoA_Reset(Stream& link, uint32_t nowMs) {
  // Header:
  // VER(1), MSG(1), LEN(1), reserved(2)
  uint8_t hdr[5];
  hdr[0] = B2A_VER;
  hdr[1] = B2A_MSG_RESET;
  hdr[2] = B2A_RESET_LEN;
  hdr[3] = 0;
  hdr[4] = 0;

  // payload[0] = 1 이면 핀보드 소프트웨어 리셋
  // payload[1] = 예약
  // payload[2..5] = 센서보드 timestamp
  uint8_t payload[B2A_RESET_LEN];

  payload[0] = 1;
  payload[1] = 0;

  payload[2] = (uint8_t)(nowMs & 0xFF);
  payload[3] = (uint8_t)((nowMs >> 8) & 0xFF);
  payload[4] = (uint8_t)((nowMs >> 16) & 0xFF);
  payload[5] = (uint8_t)((nowMs >> 24) & 0xFF);

  uint8_t crcBuf[5 + B2A_RESET_LEN];

  memcpy(crcBuf, hdr, 5);
  memcpy(crcBuf + 5, payload, B2A_RESET_LEN);

  uint16_t crc = crc16_ccitt(crcBuf, sizeof(crcBuf));

  link.write(B2A_SYNC1);
  link.write(B2A_SYNC2);
  link.write(hdr, sizeof(hdr));
  link.write(payload, sizeof(payload));

  link.write((uint8_t)(crc & 0xFF));
  link.write((uint8_t)((crc >> 8) & 0xFF));
}

// payload (6B):
//  [0] deployed(1: true / 0: false)
//  [1] reserved
//  [2..5] timeMs (uint32_t)  // B보드 기준 타임스탬프
void sendBtoA_ClimbRate(Stream& link, float climbRate_mps, uint32_t nowMs) {
  static uint32_t climbTxCount = 0;
climbTxCount++;

// if (climbTxCount % 20 == 0) {
//   Serial.print("CLIMB TX=");
//   Serial.print(climbTxCount);
//   Serial.print("  value=");
//   Serial.println(climbRate_mps, 2);
// }
  // 송신 범위: -327.67 ~ +327.67 m/s
  climbRate_mps = constrain(climbRate_mps, -327.67f, 327.67f);

  // m/s -> m/s × 100 정수
  int16_t climbRateX100 = (int16_t)lroundf(climbRate_mps * 100.0f);

  // Header:
  // VER(1), MSG(1), LEN(1), reserved(2)
  uint8_t hdr[5];
  hdr[0] = B2A_VER;
  hdr[1] = B2A_MSG_CLIMBRATE;
  hdr[2] = B2A_CLIMB_LEN;
  hdr[3] = 0;
  hdr[4] = 0;

  // Payload:
  // [0..1] climbRate × 100, int16
  // [2..5] timestamp, uint32
  uint8_t payload[B2A_CLIMB_LEN];

  wr_i16_le(&payload[0], climbRateX100);

  payload[2] = (uint8_t)(nowMs & 0xFF);
  payload[3] = (uint8_t)((nowMs >> 8) & 0xFF);
  payload[4] = (uint8_t)((nowMs >> 16) & 0xFF);
  payload[5] = (uint8_t)((nowMs >> 24) & 0xFF);

  // CRC 대상: header + payload
  uint8_t crcBuf[5 + B2A_CLIMB_LEN];

  memcpy(crcBuf, hdr, 5);
  memcpy(crcBuf + 5, payload, B2A_CLIMB_LEN);

  uint16_t crc = crc16_ccitt(crcBuf, sizeof(crcBuf));

  // 실제 송신
  link.write(B2A_SYNC1);
  link.write(B2A_SYNC2);
  link.write(hdr, sizeof(hdr));
  link.write(payload, sizeof(payload));

  // CRC little-endian
  link.write((uint8_t)(crc & 0xFF));
  link.write((uint8_t)((crc >> 8) & 0xFF));
}


// sd
struct LogHeader {
  char magic[4];     // "RLG1"
  uint16_t version;  // 1
  uint16_t recSize;  // sizeof(FlightData)
};
#pragma pack(pop)

// ================== 512B 버퍼링 ==================
static uint8_t sdBuf[512];
static uint16_t sdWp = 0;

void sdLogWrite(const void* data, uint16_t len) {
  const uint8_t* p = (const uint8_t*)data;

  if (len > sizeof(sdBuf)) {  // 안전장치
    if (sdWp) {
      logFile.write(sdBuf, sdWp);
      sdWp = 0;
    }
    logFile.write(p, len);
    return;
  }

  if (sdWp + len > sizeof(sdBuf)) {
    logFile.write(sdBuf, sdWp);
    sdWp = 0;
  }

  memcpy(&sdBuf[sdWp], p, len);
  sdWp += len;
}

void sdLogFlush() {
  if (sdWp) {
    logFile.write(sdBuf, sdWp);
    sdWp = 0;
  }
  logFile.flush();
}

// ================== 부팅마다 새 파일 생성(삭제 없음) ==================
uint16_t readBootIndex() {
  uint16_t idx;
  EEPROM.get(EEPROM_ADDR_IDX, idx);
  if (idx == 0xFFFF) idx = 0;
  return idx;
}

void writeBootIndex(uint16_t idx) {
  EEPROM.put(EEPROM_ADDR_IDX, idx);
}

bool openNewLogFile() {
  uint16_t idx = readBootIndex();
  char name[13];

  bool found = false;
  for (uint16_t tries = 0; tries < 10000; tries++) {
    snprintf(name, sizeof(name), "FL%04u.BIN", idx);
    if (!sd.exists(name)) {
      found = true;
      break;
    }
    idx = (idx + 1) % 10000;
  }
  if (!found) return false;

  logFile = sd.open(name, O_WRONLY | O_CREAT | O_EXCL);
  if (!logFile) return false;

  writeBootIndex((idx + 1) % 10000);

  LogHeader hdr{ { 'R', 'L', 'G', '1' }, 1, (uint16_t)sizeof(FlightData) };
  logFile.write((uint8_t*)&hdr, sizeof(hdr));
  logFile.flush();

  Serial.print("LOG FILE: ");
  Serial.println(name);

  return true;
}



// ============================================================================
// 피에조 부저: 상태별 비프 패턴 (non-blocking)
// ============================================================================
struct BuzzStep { uint16_t freq; uint16_t toneDur; uint16_t pauseDur; };

// freq=0 이면 noTone (쉬기)
static const BuzzStep BUZZ_STANDBY[] = {
  {880, 100, 1900},   // 단일 삑, 2초마다
};
static const BuzzStep BUZZ_POWERED[] = {
  {2500, 50, 50},     // 빠른 고음 연속
};
static const BuzzStep BUZZ_COASTING[] = {
  {1500, 80, 80},
  {1500, 80, 260},    // 이중 삑, 500ms마다
};
static const BuzzStep BUZZ_APOGEE[] = {
  {1200, 60, 60},
  {1200, 60, 60},
  {1200, 60, 700},    // 3연속 삑, 1초마다
};
static const BuzzStep BUZZ_DESCENT[] = {
  {1000, 80, 60},
  {600,  80, 180},    // 고→저 이중 삑, 400ms마다
};
static const BuzzStep BUZZ_LANDED[] = {
  {2000, 200, 100},
  {1000, 200, 100},   // 고저 교차
};

void updateBuzzer(FlightState state, uint32_t nowMs) {
  static FlightState lastState = STANDBY;
  static uint8_t step = 0;
  static uint32_t nextMs = 0;

  if (state != lastState) {
    lastState = state;
    step = 0;
    nextMs = nowMs;
    noTone(PIN_BUZZER);
  }

  if (nowMs < nextMs) return;

  const BuzzStep* pat;
  uint8_t len;
  switch (state) {
    case STANDBY:  pat = BUZZ_STANDBY;  len = 1; break;
    case POWERED:  pat = BUZZ_POWERED;  len = 1; break;
    case COASTING: pat = BUZZ_COASTING; len = 2; break;
    case APOGEE:   pat = BUZZ_APOGEE;   len = 3; break;
    case DESCENT:  pat = BUZZ_DESCENT;  len = 2; break;
    case LANDED:   pat = BUZZ_LANDED;   len = 2; break;
    default: return;
  }

  const BuzzStep& s = pat[step];
  tone(PIN_BUZZER, s.freq, s.toneDur);
  nextMs = nowMs + s.toneDur + s.pauseDur;
  step = (step + 1) % len;
}

void setup() {

  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);

  // A2B 링크: Serial3 (B: RX3=15, TX3=14)
  initLora();
  Serial3.begin(115200);

  Wire.begin();
  Wire.setClock(100000);

  // 초기값
  flight.state = STANDBY;
  flight.timeMs = 0;
  flight.roll = flight.pitch = flight.yaw = 0.0f;
  flight.filterRoll = 0.0f;
  flight.gps.latitudeE7 = 0;
  flight.gps.longitudeE7 = 0;
  flight.gps.fix = false;

  // GPS
  initGps();

  // Baro
  if (!initBaro()) {
    Serial.println("BMP280 init FAIL");
  } else {
    Serial.println("BMP280 OK -> calibrate p0...");
    calibrateBaroP0(3000);
    Serial.print("p0_hPa=");
    Serial.println(g_p0_hPa, 2);
  }
  
pinMode(53, OUTPUT);        // Mega의 하드웨어 SS핀
pinMode(SD_CS_PIN, OUTPUT); // SD CS핀 = 10번
digitalWrite(SD_CS_PIN, HIGH);
  // sd
  // sd
  // SdFat 초기화
  // 64GB 카드는 보통 exFAT이므로 SdFs를 사용함.
  // 배선이 길거나 초기화가 불안정하면 SD_SCK_MHZ(4) 또는 SD_SCK_MHZ(1)로 낮춰보기.
  if (!sd.begin(SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(8)))) {
    Serial.println("SdFat init failed!");
    sd.initErrorPrint(&Serial);
    while (1)
      ;
  }

  if (!openNewLogFile()) {
    Serial.println("log file open failed!");
    while (1)
      ;
  }

  Serial.println("SD logging started.");

  //낙하산
  pinMode(PIN_CONNECT_DETECT, INPUT_PULLUP);  //낙하산 커넥트핀 상태 설정
  initParachuteDeploy();                      //서보모터 초기화

  flight.state = STANDBY;
  //jc = {};
}

void loop() {
  uint32_t nowMs = millis();
  flight.timeMs = nowMs;
  while (Serial1.available())
      gps.encode(Serial1.read());

  handleLoraRxCommand();  // 지상국 명령 수신
  updateBuzzer(flight.state, nowMs);
  // // if(Serial2.available())
  // //   Serial.println("asdfasdf");

  // // 1) A2B 패킷은 가능한 자주 파싱
 parseAtoB(Serial3, flight, nowMs);

  // // 2) 센서 갱신
   updateBaro(flight, nowMs);
   updateGps(flight, nowMs);

// BMP280 상승률을 핀보드로 20 Hz 전송
if (nowMs - g_lastClimbTxMs >= CLIMB_TX_PERIOD_MS) {
  g_lastClimbTxMs = nowMs;

  sendBtoA_ClimbRate(
    Serial3,
    flight.baro.climbRate,
    nowMs
  );
}


  sendLoraFromFlight(flight, g_parachuteDeployed, pinDetached, ejectBtnClicked, extra1, chuteByDescent, chuteByTimer);

  if (!pinDetached) {
    pinDetached = isConnectOrDeteached(PIN_CONNECT_DETECT);
  }

  // ========================
  // // 4. 판단 및 상태 전이
  // // ========================
  // ==============시간 측정 시작(커넥트핀 분리 && imu 가속도값)==========
  if (!launchTimeStarted && pinDetached 
      && ((flight.imu.ax) * (flight.imu.ax) +
         (flight.imu.ay) * (flight.imu.ay) + 
         (flight.imu.az) * (flight.imu.az) >  
         (9.8 * 2) * (9.8 * 2))) { //이거 나중에 수정해야 함
    launchTimeStarted = true;
    launchTimeMs = millis();  // T0
    Serial.println("발사 시간 측정!");
  }

  // ========================센서 이상치 판단==========

  bool imuOMG = isOMGimu(flight.imu);
  bool baroOMG = isOMGbaro(flight.baro);

  // 2) ⛔ 센서 고장 시 APOGEE 강제 전이 (여기!)
  if ((imuOMG || baroOMG) && flight.state < APOGEE) {
    flight.state = APOGEE;
    Serial.println("센서 고장");

    // 중요: 하강 판단 누적값 리셋(권장)
    resetDecisionCounters(jc);
  }

  //================== 기본 판단 신호====================

  bool accelOver = (!imuOMG) && isAccelOver(flight.imu);

  bool altitudeUp = (!baroOMG) && isAltitudeUp(flight.baro);      // 상승 증거
  bool altitudeDown = (!baroOMG) && isAltitudeDown(flight.baro);  // 하강 증거
  bool powered = isPowered(accelOver, altitudeUp, jc);
  bool motorOver = isMotorOver(powered, jc);
  bool apogee = (flight.state < APOGEE) && altitudeUp;
  bool descent = (flight.state == APOGEE) && altitudeDown;
  // ========================

  // 4) 상태머신 갱신

  updateFlightState(
    flight,
    launchTimeStarted,
    powered,
    motorOver,
    apogee,
    descent,
    jc);

  /*===================== 낙하산 사출 함수=================
      1. 발사 10초 뒤 낙하산 사출
      2. 하강 30회 시 낙하산 사출(데이터 중복 가능성)
      =================================================*/

  if (launchTimeStarted && !deployCtl.deployed) {
    bool isCount = false;
    unsigned long flightTimeMs = millis() - launchTimeMs;

    if (flightTimeMs >= 10000 && !g_parachuteDeployed) {  // 1,000ms = 1초
      Serial.println("낙하산 사출! - 10초 조건");
      deployCtl.state = DEPLOY_PUNCH;
      if(!g_parachuteDeployed)
        chuteByTimer = true;
      g_parachuteDeployed = true;
      
    }

    if(descent)
    {
      deployCtl.state = DEPLOY_PUNCH;
      if(!g_parachuteDeployed)
        chuteByDescent = true;
      g_parachuteDeployed = true;
      Serial.println("낙하산 사출! - 고도 하강");
    }
    // if(prevClimbRate !=  flight.baro.climbRate) {
    // if(flight.baro.climbRate < -2) //하강 시 카운트 +1
    //   jc.count++;
    // else{
    //   if(jc.count > 0) //상승중이면 count가 0이상일 때만 count 1 감소
    //   jc.count--;
    // }
    // prevClimbRate = flight.baro.climbRate;
    // }

    // if(jc.count > 20 && !g_parachuteDeployed ){ //낙하산 사출
    //   deployCtl.state = DEPLOY_PUNCH;
    //   g_parachuteDeployed = true;
    //   Serial.println("낙하산 사출! - 고도 하강");
    //   }

  }

  // // ========================
  // // B -> A : parachute 상태 전송 (변화 시 버스트)
  // // ========================
  // // if (!lastParachute && g_parachuteDeployed) {
  // //   // false -> true 변화를 감지
  // //   b2aBurst = true;
  // //   b2aBurstStartMs = nowMs;
  // //   b2aLastSendMs = 0; // 즉시 한 번 보내기 위해 리셋
  // // }

  // // lastParachute = g_parachuteDeployed;

  // // // 버스트 재전송: 0.5초 동안 10Hz로만
  // // if (b2aBurst) {
  // //   if (b2aLastSendMs == 0 || (nowMs - b2aLastSendMs) >= 100) { // 100ms = 10Hz
  // //     b2aLastSendMs = nowMs;
  // //     sendBtoA_ParachuteStatus(Serial3, true, millis());
  // //   }
  // //   if (nowMs - b2aBurstStartMs >= 500) { // 0.5초 후 종료 (대략 5회)
  // //     b2aBurst = false;
  // //   }
  // // }
  if(isReset) {
    sendBtoA_Reset(Serial3, nowMs);
    isReset=false;
    delay(500);
    softwareReset();
  }

  // // ========= 낙하산 서보 FSM 실행 ========================

   applyParachuteDeployState();

  static uint32_t lastLog = 0;
  static uint32_t lastFlush = 0;
  static uint32_t lastDebugPrint = 0;

  

  //   //sd
    static uint32_t lastLogMs = 0;

    if (nowMs - lastLog >= LOG_PERIOD_MS) {
      lastLog = nowMs;
    //       Serial.print("LOG ");
    // Serial.println(nowMs);

      // memcpy(버퍼로 복사) 동안만 인터럽트 잠깐 막아서 레코드 찢김 방지
      sdLogWrite((const void*)&flight, (uint16_t)sizeof(FlightData));
    }

    // 1초마다 flush

    if (nowMs - lastFlush >= FLUSH_PERIOD_MS) {
      lastFlush = nowMs;
      sdLogFlush();
    }

  
  // ==================== Serial Plotter용 출력 ====================
// Arduino IDE 상단 메뉴:
// Tools -> Serial Plotter
// Baud rate: 115200

static uint32_t lastPlotMs = 0;

if (nowMs - lastPlotMs >= 50) {   // 20 Hz 출력
  lastPlotMs = nowMs;

  Serial.print("Alt:");
  Serial.print(flight.baro.altitude, 3);

  Serial.print("\tClimb:");
  Serial.print(flight.baro.climbRate, 3);

  Serial.print("P:");
Serial.print(flight.baro.pressure, 3);

Serial.print("\tAlt:");
Serial.print(flight.baro.altitude, 3);

Serial.print("\tClimb:");
Serial.println(flight.baro.climbRate, 3);

  Serial.println();
  
  
}

    // if (nowMs - lastDebugPrint >= 100) {
    //   lastDebugPrint = nowMs;

    //   uint32_t ageA = (flight.aRxTimeMs == 0) ? 0xFFFFFFFFUL : (nowMs - flight.aRxTimeMs);

    //   Serial.print("ageA_ms=");
    //   Serial.print(ageA);
    //   Serial.print(" roll=");
    //   Serial.print(flight.roll, 4);
    //   Serial.print(" fRoll=");
    //   Serial.print(flight.filterRoll, 2);
    //   Serial.print(" pitch=");
    //   Serial.print(flight.pitch, 4);
    //   Serial.print(" yaw=");
    //   Serial.print(flight.yaw, 4);
    //   Serial.print(" veltotal=");
    //   Serial.print(flight.veltotal, 4);
    //   Serial.print(" imuvel=");
    //   Serial.print(flight.velimu, 4);

    //   Serial.print(" | ax=");
    //   Serial.print(flight.imu.ax, 1);
    //   Serial.print(" ay=");
    //   Serial.print(flight.imu.ay, 1);
    //   Serial.print(" az=");
    //   Serial.print(flight.imu.az, 1);

    //   Serial.print(" | gx=");
    //   Serial.print(flight.imu.gx, 1);
    //   Serial.print(" gy=");
    //   Serial.print(flight.imu.gy, 1);
    //   Serial.print(" gz=");
    //   Serial.print(flight.imu.gz, 1);

    //   Serial.println();

    //   Serial.print(" | Connect =");
    //   Serial.print(pinDetached);
    //   Serial.print(" parachute =");
    //   Serial.print(g_parachuteDeployed);
    //   Serial.print(" | State = ");
    //   Serial.println(flight.state);

    //   Serial.print(" | Baro Alt=");
    //   Serial.print(flight.baro.altitude, 2);
    //   Serial.print(" climbRate =");
    //   Serial.print(flight.baro.climbRate, 2);

    //   Serial.print(" | GPS fix=");
    //   Serial.print(flight.gps.fix);
    //   Serial.print(" sats=");
    //   Serial.print(flight.gps.sats);
    //   Serial.print(" latE7=");
    //   Serial.print(flight.gps.latitudeE7);
    //   Serial.print(" lonE7=");
    //   Serial.print(flight.gps.longitudeE7);
    //   Serial.println();
    // }

//     static uint32_t cnt = 0;

// cnt++;

// if (millis() % 1000 < 5) {
//     Serial.println(cnt);
//     cnt = 0;
// }

  //   static unsigned long lastPrint = 0;
  // if (millis() - lastPrint > 1000) {   // 1초마다 출력
  //   lastPrint = millis();

  //   Serial.println("---------------");

  //   // Fix 상태
  //   Serial.print("Fix: ");
  //   if (gps.location.isValid()) {
  //     Serial.println("YES");
  //   } else {
  //     Serial.println("NO");
  //   }

  //   // 위도
  //   Serial.print("Latitude: ");
  //   if (gps.location.isValid())
  //     Serial.println(gps.location.lat(), 6);
  //   else
  //     Serial.println("N/A");

  //   // 경도
  //   Serial.print("Longitude: ");
  //   if (gps.location.isValid())
  //     Serial.println(gps.location.lng(), 6);
  //   else
  //     Serial.println("N/A");

  //   // 고도
  //   Serial.print("Altitude (m): ");
  //   if (gps.altitude.isValid())
  //     Serial.println(gps.altitude.meters());
  //   else
  //     Serial.println("N/A");

  //   // 속도
  //   Serial.print("Speed (km/h): ");
  //   if (gps.speed.isValid())
  //     Serial.println(gps.speed.kmph());
  //   else
  //     Serial.println("N/A");

  //   // 위성 수
  //   Serial.print("Satellites: ");
  //   if (gps.satellites.isValid())
  //     Serial.println(gps.satellites.value());
  //   else
  //     Serial.println("N/A");

  //   // HDOP
  //   Serial.print("HDOP: ");
  //   if (gps.hdop.isValid())
  //     Serial.println(gps.hdop.hdop());
  //   else
  //     Serial.println("N/A");

  //   // 날짜
  //   Serial.print("Date: ");
  //   if (gps.date.isValid()) {
  //     Serial.print(gps.date.year());
  //     Serial.print("/");
  //     Serial.print(gps.date.month());
  //     Serial.print("/");
  //     Serial.println(gps.date.day());
  //   } else {
  //     Serial.println("N/A");
  //   }

  //   // 시간
  //   Serial.print("Time (UTC): ");
  //   if (gps.time.isValid()) {
  //     Serial.print(gps.time.hour());
  //     Serial.print(":");
  //     Serial.print(gps.time.minute());
  //     Serial.print(":");
  //     Serial.println(gps.time.second());
  //   } else {
  //     Serial.println("N/A");
  //   }

  //   // 진단 메시지
  //   if (!gps.location.isValid()) {
  //     Serial.println(">> Waiting for GPS Fix...");
  //   }

  //   if (gps.satellites.isValid() && gps.satellites.value() == 0) {
  //     Serial.println(">> No satellites detected.");
  //   }

  //   if (gps.hdop.hdop() > 5.0 && gps.hdop.isValid()) {
  //     Serial.println(">> Poor satellite geometry.");
  //   }

  //   Serial.println("---------------\n");
  // }
}

