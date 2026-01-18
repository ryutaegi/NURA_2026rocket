#include <Arduino.h>
#include "EarthFrame.h"

void setup() {
  Serial.begin(115200);
}

void loop() {
  // 예시: 실제로는 센서에서 gx/gy/gz와 dt를 계산해야 함
  float gx = 0.0f, gy = 0.0f, gz = 0.0f;
  float dt = 0.01f;

  // EarthFrame 업데이트 (DCM 적분 + Euler 변환 + flight 저장)
  updateAttitudeAndStore(gx, gy, gz, dt);

  // 출력
  Serial.print("Earth Roll: ");
  Serial.print(flight.roll, 2);
  Serial.print(" | Earth Pitch: ");
  Serial.println(flight.pitch, 2);

  delay(10);
}
