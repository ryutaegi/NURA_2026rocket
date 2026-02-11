#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>

#include "lora.h"
#include "parachute.h"
#include "flightType.h"


#define PIN_CONNECT_DETECT 2

File logFile;
static const int SD_CS_PIN = 10;
//커넥트핀 연결을 단 한번만 판단하게함
bool pinDetached = false;  
bool g_parachuteDeployed = false; //낙하산 사출 여부

FlightData flight;
// 1) BMP280
// ============================================================================
Adafruit_BMP280 bmp;

static const uint32_t BARO_PERIOD_MS = 50; // 20Hz
static uint32_t g_baro_lastMs = 0; // 마지막으로 updateBaro()가 실제로 센서를 읽은 시간을 저장하는 변수.

// 상대고도 기준압 p0 (발사대에서 평균낸 압력)
static float g_p0_hPa = 1013.25f; // 기준 압력 p0

// climbRate 계산
static float g_alt_prev = 0.0f; // 직전고도값
static uint32_t g_alt_prevMs = 0; // 직전고도측정했던시간
static float g_climb_filt = 0.0f; // LPFT쓴 필터 상태값
static const float CLIMB_ALPHA = 0.2f; // LPF 알파값

static bool isValidPressure_hPa(float p) { return (p >= 300.0f && p <= 1100.0f); } // 기압 범위가 300~1100인지 확인

// 표준대기 근사식: p0를 발사대 압력으로 잡으면 상대고도
static float altitudeFromPressure(float p_hPa, float p0_hPa) {
  if (p_hPa <= 0.0f || p0_hPa <= 0.0f) return 0.0f; // 0이하값은 0으로
  return 44330.0f * (1.0f - powf(p_hPa / p0_hPa, 0.1903f)); // 표준대기근사식으로 고도계산
}

bool initBaro() {
  if (!bmp.begin(0x76)) {
    if (!bmp.begin(0x77)) return false;
  }
  bmp.setSampling( // BMP280 내부 설정값
    Adafruit_BMP280::MODE_NORMAL,
    Adafruit_BMP280::SAMPLING_X2,
    Adafruit_BMP280::SAMPLING_X16,
    Adafruit_BMP280::FILTER_X16,
    Adafruit_BMP280::STANDBY_MS_63
  );
  return true;
}

// 부팅 직후 몇 초간 압력 평균, g_p0_hPa 설정 (상대고도 0 기준)
void calibrateBaroP0(uint32_t calibMs = 3000) {
  uint32_t t0 = millis(); // 부팅 후 경과시간
  uint32_t n = 0; // 샘플개수
  double sum = 0; // 압력 합

  while (millis() - t0 < calibMs) {
    float p = bmp.readPressure() / 100.0f; // Pa를 hPa로
    if (isValidPressure_hPa(p)) { sum += p; n++; }
    delay(20);
  }
  if (n > 10) g_p0_hPa = (float)(sum / (double)n);
}

void updateBaro(FlightData& f, uint32_t nowMs) {
  if (nowMs - g_baro_lastMs < BARO_PERIOD_MS) return; // 주기 유지(20Hz)
  g_baro_lastMs = nowMs; // 마지막 실행시간 갱신

  float tempC = bmp.readTemperature();
  float press_hPa = bmp.readPressure() / 100.0f;
  if (!isValidPressure_hPa(press_hPa)) return; // 이상치 스킵

  float alt_m = altitudeFromPressure(press_hPa, g_p0_hPa); // 고도계산

  // 상승률 계산 + 1차 LPF
  float climb = f.baro.climbRate;
  if (g_alt_prevMs != 0) {
    float dt = (nowMs - g_alt_prevMs) / 1000.0f; // s로 변환
    if (dt > 0.005f) { // 최소 dt값(5ms)
      float raw = (alt_m - g_alt_prev) / dt; // 상승률
      g_climb_filt = (1.0f - CLIMB_ALPHA) * g_climb_filt + CLIMB_ALPHA * raw; // LPF적용
      climb = g_climb_filt;
    }
  }
  g_alt_prev = alt_m; // 현재고도 저장
  g_alt_prevMs = nowMs; // 현재시간 저장
// 구조체에 저장
  f.baro.temperature = tempC;
  f.baro.pressure    = press_hPa;
  f.baro.altitude    = alt_m;
  f.baro.climbRate   = climb;
  f.baroTimeMs = nowMs;
}

// 2) GPS
// ============================================================================
TinyGPSPlus gps;

static const uint32_t GPS_PERIOD_MS = 200; // 구조체 업데이트 주기(5Hz)
static uint32_t g_gps_lastMs = 0; // 마지막 구조체 반영 시각
static uint32_t g_lastGpsUpdateMs = 0; // 위/경도 실제 갱신 시각
// 위/경도 정수변환
static int32_t toE7(double deg) {
  double v = deg * 1e7;
  if (v >  2147483647.0) v =  2147483647.0; // int32_t범위로 제한
  if (v < -2147483648.0) v = -2147483648.0;
  v = (v >= 0.0) ? (v + 0.5) : (v - 0.5); // 반올림
  return (int32_t)v;
}

void initGps() {
  Serial1.begin(9600);
}

// loop에서 가능한 자주 파서에 먹이기
void pollGpsParser() {
  while (Serial1.available()) gps.encode(Serial1.read()); // 수신버퍼에서 1바이트 꺼내서 파서에 먹임
}

void updateGps(FlightData& f, uint32_t nowMs) {
  pollGpsParser(); // 계속 파싱해서 구조체 비우기
  if (nowMs - g_gps_lastMs < GPS_PERIOD_MS) return; // 주기유지(200ms)
  g_gps_lastMs = nowMs; // 타임스탬프 갱신

// fix판단
  bool hasLoc = gps.location.isValid();
  bool hasFix = hasLoc && (gps.location.age() < 2000); // 마지막 위치 업데이트 경과시간 2초이내면 fix
  f.gps.fix = hasFix;
// 위성 수 판단
  f.gps.sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
//Serial.println(gps.location.isValid());
  if (hasLoc) {
    f.gps.latitudeE7  = toE7(gps.location.lat());
    f.gps.longitudeE7 = toE7(gps.location.lng());
    
    if (gps.location.isUpdated()) flight.gpsTimeMs = nowMs;
  }

  if (gps.altitude.isValid()) f.gps.altitude = gps.altitude.meters(); // m
  if (gps.speed.isValid())    f.gps.speed    = gps.speed.mps(); // m/s
  if (gps.course.isValid())   f.gps.heading  = gps.course.deg(); // deg
}

// ============================================================================
// 3) A2B UART 패킷 수신/파싱
//    IMU + roll/pitch/yaw + filterRoll 갱신
// ============================================================================

// 패킷 구성 (변경 요)
static const uint8_t SYNC1 = 0xA5;
static const uint8_t SYNC2 = 0x5A;
static const uint8_t VER   = 1;
static const uint8_t MSG   = 0x21;
static const uint8_t LEN   = 20;

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
static inline int16_t rd_i16_le(const uint8_t* p) { return (int16_t)rd_u16_le(p); }
static inline uint32_t rd_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ====== Parser: call very often ======
void parseAtoB(Stream& link, FlightData& f, uint32_t nowB_ms) {
  enum { WAIT_S1, WAIT_S2, READ_HDR, READ_BODY } static st = WAIT_S1;

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
        if (b == SYNC2) { st = READ_HDR; hdrIdx = 0; }
        else st = WAIT_S1;
        break;

      case READ_HDR:
        hdr[hdrIdx++] = b;
        if (hdrIdx >= sizeof(hdr)) {
          uint8_t ver = hdr[0], msg = hdr[1], len = hdr[2];
          if (ver != VER || msg != MSG || len != LEN) { st = WAIT_S1; break; }
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
            int16_t ax10 = rd_i16_le(&payload[idx]); idx += 2;
            int16_t ay10 = rd_i16_le(&payload[idx]); idx += 2;
            int16_t az10 = rd_i16_le(&payload[idx]); idx += 2;

            // gyro: (deg/s*10) -> deg/s
            int16_t gx10 = rd_i16_le(&payload[idx]); idx += 2;
            int16_t gy10 = rd_i16_le(&payload[idx]); idx += 2;
            int16_t gz10 = rd_i16_le(&payload[idx]); idx += 2;

            // angles: (deg*100) -> deg
            int16_t roll100  = rd_i16_le(&payload[idx]); idx += 2;
            int16_t froll100 = rd_i16_le(&payload[idx]); idx += 2;
            int16_t pitch100 = rd_i16_le(&payload[idx]); idx += 2;
            int16_t yaw100   = rd_i16_le(&payload[idx]); idx += 2;

            f.imu.ax = ax10 / 100.0f;
            f.imu.ay = ay10 / 100.0f;
            f.imu.az = az10 / 100.0f;

            f.imu.gx = gx10 / 10.0f;
            f.imu.gy = gy10 / 10.0f;
            f.imu.gz = gz10 / 10.0f;

            f.roll       = roll100 / 100.0f;
            f.filterRoll = froll100 / 100.0f;
            f.pitch      = pitch100 / 100.0f;
            f.yaw        = yaw100 / 100.0f;
          }

          st = WAIT_S1;
        }
        break;
    }
  }
}






void setup() {

  Serial.begin(115200);
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

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD init failed!");
  }
  logFile = SD.open("flight.bin", FILE_WRITE);
  if (!logFile) {
    Serial.println("log file open failed!");
  }

  Serial.println("SD logging started.");


  // Baro
  if (!initBaro()) {
    Serial.println("BMP280 init FAIL");
  } else {
    Serial.println("BMP280 OK -> calibrate p0...");
    calibrateBaroP0(3000);
    Serial.print("p0_hPa="); Serial.println(g_p0_hPa, 2);
  }

//낙하산

  pinMode(PIN_CONNECT_DETECT, INPUT_PULLUP); //낙하산 커넥트핀 상태 설정

}

void loop() {
  uint32_t nowMs = millis();
  flight.timeMs = nowMs;

  handleLoraRxCommand(); // 지상국 명령 수신
  // if(Serial2.available())
  //   Serial.println("asdfasdf");

  // 1) A2B 패킷은 가능한 자주 파싱
  parseAtoB(Serial3, flight, nowMs);

  // 2) 센서 갱신
  updateBaro(flight, nowMs);
  updateGps(flight, nowMs);
  //Serial2.print("AT+SEND=1,1,1");

  // if(Serial2.available())
  // Serial.write(Serial2.read());
  // if(Serial.available())
  // Serial2.write(Serial.read());

  sendLoraFromFlight(flight, g_parachuteDeployed, pinDetached);


  if (!pinDetached) {
    pinDetached = isConnectOrDeteached(PIN_CONNECT_DETECT);
  }

 // ========================
// // 4. 판단 및 상태 전이
// // ========================
 

 bool accelOver = isAccelOver(flight.imu);
 bool pressureDown = isPressureDown(flight.baro);
 bool startFlight = isStartFlight(pinDetached, accelOver);
 bool powered = isPowred(accelOver, pressureDown);
 bool motorOver = isMotorOver(powered);
 bool apogee = isApogee(pressureDown);

 updateFlightState(flight, startFlight, powered, motorOver, apogee);

   // 디버그(0.5초마다)
   static uint32_t lastPrint = 0;
   if (nowMs - lastPrint >= 500) {
     lastPrint = nowMs;

     uint32_t ageA = (flight.aRxTimeMs == 0) ? 0xFFFFFFFFUL : (nowMs - flight.aRxTimeMs);

     Serial.print("ageA_ms="); Serial.print(ageA);
     Serial.print(" roll="); Serial.print(flight.roll, 2);
     Serial.print(" fRoll="); Serial.print(flight.filterRoll, 2);
     Serial.print(" pitch="); Serial.print(flight.pitch, 2);
     Serial.print(" yaw="); Serial.print(flight.yaw, 2);

     Serial.print(" | ax="); Serial.print(flight.imu.ax, 1);
     Serial.print(" ay="); Serial.print(flight.imu.ay, 1);
     Serial.print(" az="); Serial.print(flight.imu.az, 1);

     Serial.print(" | gx="); Serial.print(flight.imu.gx, 1);
     Serial.print(" gy="); Serial.print(flight.imu.gy, 1);
     Serial.print(" gz="); Serial.print(flight.imu.gz, 1);

     Serial.println();
     Serial.print(" | Connect ="); Serial.print(pinDetached);
     Serial.print(" parachute ="); Serial.print(g_parachuteDeployed);

     Serial.print(" | Baro Alt="); Serial.print(flight.baro.altitude, 2);
     Serial.print(" Vz="); Serial.print(flight.baro.climbRate, 2);

     Serial.print(" | GPS fix="); Serial.print(flight.gps.fix);
     Serial.print(" sats="); Serial.print(flight.gps.sats);
     Serial.print(" latE7="); Serial.print(flight.gps.latitudeE7);
     Serial.print(" lonE7="); Serial.print(flight.gps.longitudeE7);
     Serial.println();
   }
   static uint32_t lastLogMs = 0;
  if (nowMs - lastLogMs >= 50) { // 예: 20Hz 로깅
    lastLogMs = nowMs;

    logFile.write((uint8_t*)&flight, sizeof(FlightData));
  }
  static uint32_t lastFlushMs = 0;
  if (nowMs - lastFlushMs >= 1000) { // 1초마다만 flush
    lastFlushMs = nowMs;
    logFile.flush();
  }


}
