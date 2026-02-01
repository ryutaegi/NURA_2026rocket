// 아두이노 예제 코드
// 센서 데이터를 효율적인 바이너리 패킷으로 시리얼 통신을 통해 전송합니다.

// 데이터 패킷 구조체 정의
// __attribute__((packed))는 컴파일러가 임의로 패딩을 추가하지 못하게 하여
// 송/수신간의 데이터 크기를 정확히 일치시킵니다.
struct __attribute__((packed)) FlightDataPacket {
  // 시작을 알리는 Sync Byte (1 byte)
  const uint8_t start_byte = 0xAA;

  // 데이터 필드 (34 bytes)
  float roll;
  float pitch;
  float yaw;
  float lat;
  float lon;
  float alt;
  float temp;
  float hum;
  uint8_t para;  // 0 or 1
  uint8_t phase; // FlightState enum 값

  // 간단한 체크섬 (모든 데이터 바이트를 더한 값) (1 byte)
  uint8_t checksum;
}; // 총 36 바이트

// 패킷 생성 및 전송
void sendData() {
  FlightDataPacket packet;

  // 랜덤 값으로 센서 데이터 채우기
  packet.roll = random(-18000, 18000) / 100.0;
  packet.pitch = random(-18000, 18000) / 100.0;
  packet.yaw = random(0, 36000) / 100.0;
  packet.lat = random(37000000, 38000000) / 1000000.0;
  packet.lon = random(126000000, 127000000) / 1000000.0;
  packet.alt = random(0, 50000) / 100.0;
  packet.temp = random(1000, 3000) / 100.0;
  packet.hum = random(2000, 8000) / 100.0;
  packet.para = random(0, 2);   // 0 or 1
  packet.phase = random(0, 7);  // 0 to 6 (FlightState)

  // 체크섬 계산
  packet.checksum = 0;
  uint8_t* bytes = (uint8_t*)&packet;
  // start_byte는 제외하고 checksum 필드 전까지 더합니다.
  for (size_t i = 1; i < sizeof(packet) - 1; ++i) {
    packet.checksum += bytes[i];
  }

  // 시리얼 포트로 패킷 전송
  Serial.write((uint8_t*)&packet, sizeof(packet));
}

void setup() {
  // 효율을 위해 통신 속도를 높입니다. server.js와 동일하게 맞춰야 합니다.
  Serial.begin(115200); 
  randomSeed(analogRead(0));
}

void loop() {
  sendData();
  delay(100); // 0.1초마다 전송
}
