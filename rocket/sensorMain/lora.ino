#include <Arduino.h>
#include <Wire.h>
#include "lora.h"




// ======================= LoRa 설정 =======================
#define LORA_PORT  Serial2
static const uint32_t LORA_BAUD = 28800;
static const uint8_t  LORA_ADDR = 0;            // AT+SEND=0,...
static const uint32_t LORA_PERIOD_MS = 200;     //  송신 hz

// ======================= base64 =======================
static const char b64_tbl[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// String base64Encode(const uint8_t* data, int len) {
//   String out;
//   out.reserve(((len + 2) / 3) * 4);
//   for (int i = 0; i < len; i += 3) {
//     uint32_t n = ((uint32_t)data[i] << 16);
//     if (i + 1 < len) n |= ((uint32_t)data[i + 1] << 8);
//     if (i + 2 < len) n |= data[i + 2];

//     out += b64_tbl[(n >> 18) & 0x3F];
//     out += b64_tbl[(n >> 12) & 0x3F];
//     out += (i + 1 < len) ? b64_tbl[(n >> 6) & 0x3F] : '=';
//     out += (i + 2 < len) ? b64_tbl[n & 0x3F] : '=';
//   }
//   return out;
// }


int base64Encode(const uint8_t* data, int len, char* out) {
  int outIdx = 0;

  for (int i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t)data[i] << 16);
    if (i + 1 < len) n |= ((uint32_t)data[i + 1] << 8);
    if (i + 2 < len) n |= data[i + 2];

    out[outIdx++] = b64_tbl[(n >> 18) & 0x3F];
    out[outIdx++] = b64_tbl[(n >> 12) & 0x3F];
    out[outIdx++] = (i + 1 < len) ? b64_tbl[(n >> 6) & 0x3F] : '=';
    out[outIdx++] = (i + 2 < len) ? b64_tbl[n & 0x3F] : '=';
  }

  out[outIdx] = '\0';
  return outIdx;
}

// ======================= packing utils (코드 스타일 유지: Big-endian) =======================
static inline float clamp_f(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static inline uint8_t clamp_u8_i(int32_t x, int32_t lo, int32_t hi) {
  if (x < lo) x = lo;
  if (x > hi) x = hi;
  return (uint8_t)x;
}

static inline int16_t clamp_i16(int32_t x) {
  if (x > 32767) return 32767;
  if (x < -32768) return -32768;
  return (int16_t)x;
}
static inline uint16_t clamp_u16(int32_t x) {
  if (x > 65535) return 65535;
  if (x < 0) return 0;
  return (uint16_t)x;
}


static inline int32_t iround(float x) { return (x >= 0.0f) ? (int32_t)(x + 0.5f) : (int32_t)(x - 0.5f); }

// alt: 0 ~ 255m
static inline uint8_t encodeAlt(float alt_m) {
  int32_t raw = iround(alt_m);
  return clamp_u8_i(raw, 0, 255);
}

// temp: -20 ~ 100 degC -> 0 ~ 120
static inline uint8_t encodeTemp1C(float temp_c) {
  int32_t t = iround(temp_c);
  if (t < -20) t = -20;
  if (t > 100) t = 100;
  return (uint8_t)(t + 20);
}

// roll: -180 ~ +180 deg -> 0 ~ 255
static inline uint8_t encodeRoll8(float roll_deg) {
  float r = clamp_f(roll_deg, -180.0f, 180.0f);
  int32_t raw = iround((r + 180.0f) * 255.0f / 360.0f);
  return clamp_u8_i(raw, 0, 255);
}


// quaternion component (-1.0 ~ 1.0) -> int16
static inline int16_t encodeQuatQ15(float q) {
  q = clamp_f(q, -1.0f, 1.0f);
  return clamp_i16(iround(q * 32767.0f));
}

void push16_be(uint8_t* buf, int& idx, uint16_t v) { // unsigned 16
  buf[idx++] = (uint8_t)((v >> 8) & 0xFF);
  buf[idx++] = (uint8_t)(v & 0xFF);
}
void push16_be_i(uint8_t* buf, int& idx, int16_t v) { // signed 16
  push16_be(buf, idx, (uint16_t)v);
}

void push32_be(uint8_t* buf, int& idx, int32_t v) {
  buf[idx++] = (uint8_t)((v >> 24) & 0xFF);
  buf[idx++] = (uint8_t)((v >> 16) & 0xFF);
  buf[idx++] = (uint8_t)((v >> 8) & 0xFF);
  buf[idx++] = (uint8_t)(v & 0xFF);
}


// ======================= LoRa init =======================
void initLora() {
  LORA_PORT.begin(LORA_BAUD);
  delay(200);
}

// ======================= 핵심: FlightData -> LoRa 송신 =======================
void sendLoraFromFlight(const FlightData& f, bool g_parachuteDeployed, bool pinDetached, bool ejectBtnClicked = false,
                        bool soundBtnClicked = false,
                        bool chuteByEmergency = false,
                        bool chuteByDescent = false,
                        bool chuteByTimer = false,
                        uint8_t qIndex = 0,
                        float filterRoll = 0) {
  static uint32_t lastMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastMs < LORA_PERIOD_MS) return;
  lastMs = nowMs;

  uint8_t buf[20];
  int idx = 0;

  buf[idx++] = 0xAA; // sync

  // roll/pitch/yaw 이름을 그대로 쓰되, 실제 값은 quaternion 1/2/3
  push16_be_i(buf, idx, encodeQuatQ15(f.roll));
  push16_be_i(buf, idx, encodeQuatQ15(f.pitch));
  push16_be_i(buf, idx, encodeQuatQ15(f.yaw));

  // lat/lon: int32 E7 그대로
  push32_be(buf, idx, f.gps.latitudeE7);
  push32_be(buf, idx, f.gps.longitudeE7);

  // alt
  buf[idx++] = encodeAlt(f.baro.altitude);

  // temp
  buf[idx++] = encodeTemp1C(f.baro.temperature);

uint8_t sats = (f.gps.sats > 15) ? 15 : f.gps.sats;
buf[idx++] =
    ((sats & 0x0F) << 4) |
    ((soundBtnClicked   ? 1 : 0) << 3) |
    ((ejectBtnClicked   ? 1 : 0) << 2) |
    ((g_parachuteDeployed ? 1 : 0) << 1) |
    ((pinDetached ? 1 : 0) << 0);

uint8_t stage = ((uint8_t)f.state > 7) ? 7 : (uint8_t)f.state;
buf[idx++] =
    ((stage & 0x07) << 5) |
    ((chuteByEmergency ? 1 : 0) << 4) |
    ((chuteByDescent   ? 1 : 0) << 3) |
    ((chuteByTimer     ? 1 : 0) << 2) |
    (qIndex & 0x03);

buf[idx++] = encodeRoll8(filterRoll);

if (idx != 20) {
  Serial.print("LoRa packet size error: ");
  Serial.println(idx);
  return;
}


  // base64
  //String payload = base64Encode(buf, idx);

  // RYLR998: AT+SEND=<addr>,<len>,<data>\r\n
  //LORA_PORT.print("AT+SEND=1,1,1");

  // LORA_PORT.print("AT+SEND=");
  // LORA_PORT.print(LORA_ADDR);
  // LORA_PORT.print(",");
  // LORA_PORT.print(payload.length());
  // LORA_PORT.print(",");
  // LORA_PORT.print(payload);
  // LORA_PORT.print("\r\n");

 // Serial.println("send");

 char payload[64];
int payloadLen = base64Encode(buf, idx, payload);

char cmd[96];
int cmdLen = snprintf(cmd, sizeof(cmd),
                      "AT+SEND=%d,%d,%s\r\n",
                      LORA_ADDR,
                      payloadLen,
                      payload);

LORA_PORT.write((uint8_t*)cmd, cmdLen);
}

// Serial2(=LORA_PORT)에서 한 줄씩 받아서 +RCV 파싱
void handleLoraRxCommand() {
  while (LORA_PORT.available()) {
    //Serial.println("Serial2 available!");  // ← 이거 추가
    String line = LORA_PORT.readStringUntil('\n');
    //Serial.println(line);
    line.trim();
    if (!line.startsWith("+RCV=")) continue;
    Serial.println("입력받음");

    int p1 = line.indexOf(',');
    int p2 = line.indexOf(',', p1 + 1);
    int p3 = line.indexOf(',', p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0) continue;

    String data = line.substring(p2 + 1, p3);

    if (data == "E") {
      g_parachuteDeployed = true;
      deployCtl.state = DEPLOY_PUNCH;
      //emergencyDeploy();
      Serial.println("receive EEE");
    }
    if (data == "R") {
      Serial.println("receive RRR");
      isReset = true;
    }
  }
}


