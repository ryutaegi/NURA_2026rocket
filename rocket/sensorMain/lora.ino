#include <Arduino.h>
#include <Wire.h>
#include "lora.h"




// ======================= LoRa 설정 =======================
#define LORA_PORT  Serial2
static const uint32_t LORA_BAUD = 9600;
static const uint8_t  LORA_ADDR = 0;            // AT+SEND=1,...
static const uint32_t LORA_PERIOD_MS = 200;     // 5Hz 송신

// ======================= base64 =======================
static const char b64_tbl[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const uint8_t* data, int len) {
  String out;
  out.reserve(((len + 2) / 3) * 4);
  for (int i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t)data[i] << 16);
    if (i + 1 < len) n |= ((uint32_t)data[i + 1] << 8);
    if (i + 2 < len) n |= data[i + 2];

    out += b64_tbl[(n >> 18) & 0x3F];
    out += b64_tbl[(n >> 12) & 0x3F];
    out += (i + 1 < len) ? b64_tbl[(n >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? b64_tbl[n & 0x3F] : '=';
  }
  return out;
}

// ======================= packing utils (코드 스타일 유지: Big-endian) =======================
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

// (state, parachute) 합치기: phase*10 + (0/1)
static inline uint8_t packPhaseChute(uint8_t phase, bool chute) {
  if (phase > 25) phase = 25;   // 안전 클램프(임의)
  return (uint8_t)(phase * 10 + (chute ? 1 : 0));
}

// ======================= LoRa init =======================
void initLora() {
  LORA_PORT.begin(LORA_BAUD);
  delay(200);
}

// ======================= 핵심: FlightData -> LoRa 송신 =======================
void sendLoraFromFlight(const FlightData& f, bool parachuteDeployed, uint8_t connect = 0) {
  static uint32_t lastMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastMs < LORA_PERIOD_MS) return;
  lastMs = nowMs;

  uint8_t buf[32];
  int idx = 0;

  buf[idx++] = 0xAA; // sync

  // roll/pitch/yaw: deg * 100 -> int16
  push16_be_i(buf, idx, clamp_i16(iround(f.roll  * 100.0f)));
  push16_be_i(buf, idx, clamp_i16(iround(f.pitch * 100.0f)));
  push16_be_i(buf, idx, clamp_i16(iround(f.yaw   * 100.0f)));

  // lat/lon: int32 E7 그대로
  push32_be(buf, idx, f.gps.latitudeE7);
  push32_be(buf, idx, f.gps.longitudeE7);

  // alt: m * 10 -> uint16 (0.1m)
  push16_be(buf, idx, clamp_u16(iround(f.baro.altitude * 100.0f)));

  // temp: C * 100 -> int16
  push16_be_i(buf, idx, clamp_i16(iround(f.baro.temperature * 100.0f)));

  // connect 1byte 연결되면 0, 아니면 1
  buf[idx++] = connect;

  // state + parachute
  buf[idx++] = packPhaseChute((uint8_t)f.state, parachuteDeployed);

  // base64
  String payload = base64Encode(buf, idx);

  // RYLR998: AT+SEND=<addr>,<len>,<data>\r\n
  //LORA_PORT.print("AT+SEND=1,1,1");
  LORA_PORT.print("AT+SEND=");
  LORA_PORT.print(LORA_ADDR);
  LORA_PORT.print(",");
  LORA_PORT.print(payload.length());
  LORA_PORT.print(",");
  LORA_PORT.print(payload);
  LORA_PORT.print("\r\n");
}


