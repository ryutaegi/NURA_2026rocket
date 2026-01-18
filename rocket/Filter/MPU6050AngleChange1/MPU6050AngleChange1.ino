#include <Wire.h>
#include "MPU6050AngleChange1.h"

MPU6050AngleChange1 filter(50, 0.98f); // interval=50ms, alpha=0.98
unsigned long preMillis = 0;
const unsigned long interval = 50;
float dt_loop = 0.0f;

// 공용 데이터 구조체 (다른 팀/모듈에서 flight.imu.az 처럼 접근)
FlightData flight;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // 초기 상태 (필요하면 각 팀에서 갱신)
  flight.state = STANDBY;

  if (!filter.begin()) {
    Serial.println("MPU6050 연결 실패");
    while (1) { } // 정지
  }

  filter.calibrateGyro(200);

  Serial.println("MPU6050AngleChange start");
}

void loop() {
  // update()가 true일 때만(50ms마다) 값이 갱신됨
  if (millis() - preMillis >= interval) {
    preMillis = millis();
    dt_loop = interval / 1000.0f;

    // 1) 센서 읽고 필터 갱신 (기존 흐름 유지)
    filter.update(dt_loop);

    // 2) 로켓 프레임으로 값 바꿔줌 (기존 함수/매핑 유지)
    filter.convert(filter.getRoll(), filter.getPitch(), filter.getYaw());

    // 3) 구조체에 결과/센서값 저장
    filter.writeFlightData(flight);
    flight.timeMs = preMillis; // loop 기준 시각(원하면 millis() 그대로 써도 됨)

    // ====== 출력 (구조체 기반) ======
    // 로켓 롤축이 센서 Z축이라서 Rocket_Roll = flight.roll (== global yaw)
    Serial.print("Rocket_Roll: ");
    Serial.print(flight.roll, 2);

    Serial.print("|| rocketFrameRoll: ");
    Serial.print(flight.roll, 2);
    Serial.print(" deg ");

    Serial.print(" || rocketFramePitch: ");
    Serial.print(flight.pitch, 2);
    Serial.print(" deg ");

    Serial.print(" || rocketFrameYaw: ");
    Serial.print(flight.yaw, 2);
    Serial.print(" deg ");

    Serial.print("|| globalFrameRoll: ");
    Serial.print(flight.filter_roll, 2);
    Serial.print(" deg ");

    Serial.print(" || globalFramePitch: ");
    Serial.print(flight.pitch, 2);
    Serial.print(" deg ");

    Serial.print(" || globalFrameYaw: ");
    Serial.print(flight.roll, 2);
    Serial.print(" deg ");

    Serial.println();
  }
}
