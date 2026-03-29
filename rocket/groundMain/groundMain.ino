#include <SoftwareSerial.h>
#include <string.h>
#include <ctype.h>

SoftwareSerial lora(2, 3); // RX, TX

int ejection = false;
int sound = false;

struct __attribute__((packed)) FlightDataPacket {
  const uint8_t start_byte = 0xAA;

  int16_t q1;
  int16_t q2;
  int16_t q3;
  int32_t lat;
  int32_t lon;
  uint8_t alt;
  uint8_t temp;
  uint8_t flag1;
  uint8_t flag2;
  uint8_t roll;

  uint8_t checksum;
};

const char* b64 =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Index(char c) {
  const char* p = strchr(b64, c);
  return p ? (p - b64) : 0;
}

int base64Decode(const char* in, uint8_t* out) {
  int len = strlen(in);
  int outLen = 0;

  for (int i = 0; i < len; i += 4) {
    uint32_t n = 0;
    int pad = 0;

    for (int j = 0; j < 4; j++) {
      char c = in[i + j];
      if (c == '=') {
        n <<= 6;
        pad++;
      } else {
        n = (n << 6) | b64Index(c);
      }
    }

    out[outLen++] = (n >> 16) & 0xFF;
    if (pad < 2) out[outLen++] = (n >> 8) & 0xFF;
    if (pad < 1) out[outLen++] = n & 0xFF;
  }
  return outLen;
}

int16_t read16(const uint8_t* buf, int& idx) {
  int16_t v = (buf[idx] << 8) | buf[idx + 1];
  idx += 2;
  return v;
}

int32_t read32(const uint8_t* buf, int& idx) {
  int32_t v = 0;
  v |= (int32_t)buf[idx++] << 24;
  v |= (int32_t)buf[idx++] << 16;
  v |= (int32_t)buf[idx++] << 8;
  v |= (int32_t)buf[idx++];
  return v;
}

// trim 함수
void trim(char* str) {
  int len = strlen(str);

  int start = 0;
  while (isspace(str[start])) start++;

  int end = len - 1;
  while (end >= start && isspace(str[end])) end--;

  int i = 0;
  for (int j = start; j <= end; j++) {
    str[i++] = str[j];
  }
  str[i] = '\0';
}

// =======================
// LoRa RX 처리
// =======================
void handleLoraRx() {
  if (!lora.available()) return;

  char line[128];
  int len = lora.readBytesUntil('\n', line, sizeof(line) - 1);
  line[len] = '\0';

  trim(line);

  if (strncmp(line, "+RCV=", 5) != 0) return;

  char* p1 = strchr(line, ',');
  if (!p1) return;

  char* p2 = strchr(p1 + 1, ',');
  if (!p2) return;

  char* p3 = strchr(p2 + 1, ',');
  if (!p3) return;

  int payloadLen = p3 - (p2 + 1);
  if (payloadLen <= 0 || payloadLen > 100) return;

  char payload[128];
  strncpy(payload, p2 + 1, payloadLen);
  payload[payloadLen] = '\0';

  uint8_t raw[64];
  int rawLen = base64Decode(payload, raw);

  if (rawLen != 20) {
    Serial.print("LEN ERROR: ");
    Serial.println(rawLen);
    return;
  }

  if (raw[0] != 0xAA) {
    Serial.print("SYNC ERROR: ");
    Serial.println(raw[0], HEX);
    return;
  }

  int idx = 1;
  FlightDataPacket packet;

  // q1/q2/q3: Q15 인코딩 (*32767), 2바이트씩
  packet.q1 = read16(raw, idx);
  packet.q2 = read16(raw, idx);
  packet.q3 = read16(raw, idx);

  // lat/lon: int32 E7
  packet.lat = read32(raw, idx);
  packet.lon = read32(raw, idx);

  // alt: 1바이트 (0~255m)
  packet.alt = raw[idx++];

  // temp: 1바이트 (-20 오프셋, 0~120 → -20~100)
  packet.temp = raw[idx++];

  
  packet.flag1 = raw[idx++];
  packet.flag2 = raw[idx++];


  // connect (기존 포맷 유지: 위성개수*10 + 커넥트핀, 소리클릭시 +2/+3)
  if (sound == true) {
    packet.flag1 |= 0x20; //3번비트 1로 설정
    sound = false;
  } else {
    packet.flag1 &= ~0x20; //3번 비트 0으로 설정 
  }

 

  // para: 비상사출 버튼 클릭시 2, 아니면 패킷의 낙하산사출 비트
  if (ejection == true) {
    packet.flag1 |= 0x40; //2번비트 1로 설정
    ejection = false;
  } else {
    packet.flag1 &= ~0x40; // 2번비트 0으로 설정
  }

  // 바이트 19: roll값 (0~255 → -180~+180, 로켓 좌표계)
  packet.roll = raw[idx++];

  

  packet.checksum = 0;
  uint8_t* bytes = (uint8_t*)&packet;
  for (size_t i = 1; i < sizeof(packet) - 1; ++i) {
    packet.checksum += bytes[i];
  }

  Serial.write((uint8_t*)&packet, sizeof(packet));
}

// =======================
// 웹 명령 처리
// =======================
void handleWebCommand() {
  if (!Serial.available()) return;

  char cmd[32];
  int len = Serial.readBytesUntil('\n', cmd, sizeof(cmd) - 1);
  cmd[len] = '\0';

  trim(cmd);

  if (strcmp(cmd, "EJECT") == 0) {
    digitalWrite(13, HIGH);
    sendEmergencyDeploy();
  }
  else if (strcmp(cmd, "RESET") == 0) {
    digitalWrite(13, LOW);
    sendReset();
  }
}

// =======================
// 송신 함수
// =======================
void sendEmergencyDeploy() {
  for (int i = 0; i < 10; i++) {
    lora.print("AT+SEND=1,1,E\r\n");
    delay(50);
  }
}

void sendReset() {
  for (int i = 0; i < 10; i++) {
    lora.print("AT+SEND=1,1,R\r\n");
    delay(50);
  }
}

// =======================
// setup / loop
// =======================
void setup() {
  Serial.begin(115200);
  lora.begin(28800);
  Serial.println("RX READY");

  pinMode(13, OUTPUT);
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
}

void loop() {
  handleLoraRx();
  handleWebCommand();

  if (digitalRead(9) == LOW) {
    ejection = true;
    sendEmergencyDeploy();
  }

  if (digitalRead(8) == LOW) {
    sound = true;
  }
}