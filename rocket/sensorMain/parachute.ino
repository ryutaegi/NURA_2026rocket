#include <Arduino.h>
#include "parachute.h"


// 판단 함수
// ========================
bool isConnectOrDeteached(int connectPin)                    //분리되면 참으로 판단
{
// LOW -> 연결됨
// HIGH -> 분리됨
return (digitalRead(connectPin) == HIGH);
}


bool isAccelOver(const ImuData& imu) {                    //제곱값 비교로 바꿈
const float G = 9.81;
const float THRESHOLD_SQ = (10.5f * G) * (10.5f * G);   //임계값은 적절하게 조정하기
float magSq = imu.ax * imu.ax + imu.ay * imu.ay + imu.az * imu.az;
return magSq >= THRESHOLD_SQ;   
}

bool isPressureDown(const BaroData& baro) {         //압력이 낮아지면 참으로 판단=상승중
static float prev = 0.0f;
static bool first = true; 
if (first) {
prev = baro.pressure;                 //최초 호출시 현재 기압 저장
first = false;                       //첫 번째 읽은 기압은 거짓 판단 
return false;
}
bool down = baro.pressure < prev;
prev = baro.pressure;
return down;
}


bool isStartFlight(bool pinDetached, bool accelOver) {
return pinDetached && accelOver;
}

bool isPowred(bool accelOver, bool pressureDown) {    //가속도 판단 참, 기압 하강 참
static uint8_t count = 0;
const uint8_t THRESHOLD = 10;                 //10번 참이 나와야 추력 중으로 판단
if (accelOver && pressureDown) {
if (count < THRESHOLD) count++;
} else {
count = 0;
}
return count >= THRESHOLD;
}

bool isMotorOver(bool isPoweredNow) {             
static uint8_t count = 0;
const uint8_t THRESHOLD = 10;
if (!isPoweredNow) {
if (count < THRESHOLD) count++;
} else {
count = 0;
}
return count >= THRESHOLD;
}

bool isApogee(bool pressureDown) {
static uint8_t count = 0;
const uint8_t THRESHOLD = 10;
if (!pressureDown) {
if (count < THRESHOLD) count++;
} else {
count = 0;
}
return count >= THRESHOLD;
}


// 상태 전이 함수  //상태머신 
// ========================
void updateFlightState(FlightData &flight, bool startFlight, bool powered, bool motorOver, bool apogee) {
switch (flight.state) {
case STANDBY:
if (startFlight) {
flight.state = LAUNCHED;
Serial.println("STANDBY → LAUNCHED");
}
break;
case LAUNCHED:
if (powered) {
flight.state = POWERED;
Serial.println("LAUNCHED → POWERED");
}
break;
case POWERED:
if (motorOver) {
flight.state = COASTING;
Serial.println("POWERED → COASTING");
}
break;
case COASTING:
if (apogee) {
flight.state = APOGEE;
Serial.println("COASTING → APOGEE");
}
break;
case APOGEE:
// 향후 낙하 감지 후 DESCENT
break;
case DESCENT:
// 향후 착지 감지 후 LANDED
break;
case LANDED:
break;
}
}

const char* getStateName(FlightState state) {
switch (state) {
case STANDBY: return "STANDBY";
case LAUNCHED: return "LAUNCHED";
case POWERED: return "POWERED";
case COASTING: return "COASTING";
case APOGEE: return "APOGEE";
case DESCENT: return "DESCENT";
case LANDED: return "LANDED";
default: return "UNKNOWN";
}
}

