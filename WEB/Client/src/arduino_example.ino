// 아두이노 예제 코드
// 센서 데이터를 JSON 형식으로 시리얼 통신으로 전송

// 필요한 라이브러리 (예시)
// #include <Wire.h>
// #include <Adafruit_BMP280.h>
// #include <MPU6050.h>
// #include <TinyGPS++.h>

void setup() {
  Serial.begin(9600);
  
  // 센서 초기화
  // GPS, IMU, 기압계 등 초기화
}

void loop() {
  // 센서 데이터 읽기
  float latitude = 37.5665;      // GPS 위도
  float longitude = 126.9780;     // GPS 경도
  float altitude = 0;             // 기압계로부터 고도
  float speed = 0;                // GPS 속도
  float pitch = 0;                // IMU pitch
  float roll = 0;                 // IMU roll
  float yaw = 0;                  // IMU yaw
  float temperature = 22.0;       // 온도 센서
  float pressure = 1013.25;       // 기압 센서
  int battery = 100;              // 배터리 잔량

  // JSON 형식으로 데이터 전송
  Serial.print("{");
  Serial.print("\"lat\":");
  Serial.print(latitude, 6);
  Serial.print(",\"lng\":");
  Serial.print(longitude, 6);
  Serial.print(",\"alt\":");
  Serial.print(altitude, 2);
  Serial.print(",\"speed\":");
  Serial.print(speed, 2);
  Serial.print(",\"pitch\":");
  Serial.print(pitch, 2);
  Serial.print(",\"roll\":");
  Serial.print(roll, 2);
  Serial.print(",\"yaw\":");
  Serial.print(yaw, 2);
  Serial.print(",\"temp\":");
  Serial.print(temperature, 2);
  Serial.print(",\"press\":");
  Serial.print(pressure, 2);
  Serial.print(",\"battery\":");
  Serial.print(battery);
  Serial.println("}");
  
  delay(1000); // 1초마다 전송
}
