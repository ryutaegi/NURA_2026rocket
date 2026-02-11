#include <SoftwareSerial.h>

SoftwareSerial lora(2, 3); // RX, TX



struct __attribute__((packed)) FlightDataPacket {
  // 시작을 알리는 Sync Byte (1 byte)
  const uint8_t start_byte = 0xAA;

  // 데이터 필드 (42 bytes)
  float roll;
  float pitch;
  float yaw;
  float lat;
  float lon;
  float alt;
  float temp;
  float connect;
  float speed;
  float pressure;
  uint8_t para;  // 0 or 1
  uint8_t phase; // FlightState enum 값

  // 간단한 체크섬 (모든 데이터 바이트를 더한 값) (1 byte)
  uint8_t checksum;
}; // 총 44 바이트


const char* b64 =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Index(char c) {
  const char* p = strchr(b64, c);
  return p ? (p - b64) : 0;
}

int base64Decode(const String& in, uint8_t* out) {
  int outLen = 0;

  for (int i = 0; i < in.length(); i += 4) {
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

// float readFloat(const uint8_t* buf, int& idx) {
//   union { float f; uint8_t b[4]; } u;
//   for (int i = 0; i < 4; i++) u.b[i] = buf[idx++];
//   return u.f;
// }

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

void handleLoraRx() { // 로켓으로부터의 텔레메트리 수신 함수
  if (!lora.available()) return;

  String line = lora.readStringUntil('\n');
  line.trim();
  //Serial.println(line);

  if (!line.startsWith("+RCV=")) return;

  int p1 = line.indexOf(',');
  int p2 = line.indexOf(',', p1 + 1);
  int p3 = line.indexOf(',', p2 + 1);
  if (p3 < 0) return;

  String payload = line.substring(p2 + 1, p3);

  uint8_t raw[64];
  int rawLen = base64Decode(payload, raw);

  if (rawLen != 21) {
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

  // int16_t roll_i  = 
  // int16_t pitch_i = 
  // int16_t yaw_i   =

  // int32_t lat_i = read32(raw, idx);
  // int32_t lon_i = r

  // uint16_t alt_i = 
  // int16_t temp_i = 

  // uint8_t hum   = raw[idx++];
  // uint8_t phase  = 
  // uint8_t para = 

  // float roll  = roll_i  / 100.0;
  // float pitch = pitch_i / 100.0;
  // float yaw   = yaw_i   / 100.0;

  // float lat = lat_i / 1e7;
  // float lon = lon_i / 1e7;

  // float alt  = alt_i / 10.0;
  // float temp = temp_i / 100.0;

  FlightDataPacket packet;
  
  packet.roll = read16(raw, idx) / 100.0;
  packet.pitch = read16(raw, idx) / 100.0;
  packet.yaw = read16(raw, idx) / 100.0;

  packet.lat = read32(raw, idx) / 1e7;
  packet.lon = read32(raw, idx) / 1e7;
  packet.alt = read16(raw, idx) / 100;
  packet.temp = read16(raw, idx) / 100.0;

  packet.connect= raw[idx++];
  packet.phase = raw[idx] / 10;
  packet.para = raw[idx++] % 10;
  packet.pressure = random(500, 1000);   
  packet.speed = random(0, 100); 

  // 체크섬 계산
  packet.checksum = 0;
  uint8_t* bytes = (uint8_t*)&packet;
  // start_byte는 제외하고 checksum 필드 전까지 더합니다.
  for (size_t i = 1; i < sizeof(packet) - 1; ++i) {
    packet.checksum += bytes[i];
  }

  // 시리얼 포트로 패킷 전송
  //Serial.write((uint8_t*)&packet, sizeof(packet));


  Serial.print("ROLL=");  Serial.print(packet.roll);
  Serial.print(" PITCH=");Serial.print(packet.pitch);
  Serial.print(" YAW=");  Serial.print(packet.yaw);
  Serial.print(" LAT=");  Serial.print(packet.lat, 7);
  Serial.print(" LON=");  Serial.print(packet.lon, 7);
  Serial.print(" ALT=");  Serial.print(packet.alt);
  Serial.print(" TEMP="); Serial.print(packet.temp);
  Serial.print(" CONNECT=");  Serial.print(packet.connect);
  Serial.print(" PARA="); Serial.print(packet.para);
  Serial.print(" PHASE=");Serial.println(packet.phase);

  // if(Serial.available())
  // lora.write(Serial.read());
  // if(lora.available())
  // Serial.write(lora.read())
}


void handleWebCommand() { // 웹으로부터의 명령 처리 함수
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "EJECT") {
    //digitalWrite(13, HIGH);
    sendEmergencyDeploy("E");
  }
  else if(cmd == "CENTER") {
    //digitalWrite(13, LOW);
    sendEmergencyDeploy("C");
  }
}

void sendEmergencyDeploy(char c) { // LoRa 비상 사출 송신 함수
  const char* msg = c;   // 1바이트(문자 1개) 커맨드

  lora.print("AT+SEND=1,1,");
  //lora.print(strlen(msg));   // 1
  //lora.print(",");
  lora.print(msg);
  lora.print("\r\n");

  Serial.println("[GS] EMERGENCY DEPLOY SENT (\"E\")");
}


void setup() {
  Serial.begin(115200);
  lora.begin(9600);
  Serial.println("RX READY");
  //pinMode(13, OUTPUT);
}


void loop() {
   handleLoraRx();
   handleWebCommand();
  //sendEmergencyDeploy("E");
  // if(Serial.available())
  // lora.write(Serial.read());
  // if(lora.available())
  // Serial.write(lora.read());
  }

