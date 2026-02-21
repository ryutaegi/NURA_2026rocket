#ifndef PARACHUTE_H
#define PARACHUTE_H

#include <Arduino.h>
#include "flightType.h"
#include <Servo.h>
//낙하산 코드 시작

// //서보모터 각도 제어-> 나중에 각도 조절하기
const uint8_t DEPLOY_ARM_ANGLE = 180;    // 대기 10 180
const uint8_t DEPLOY_PUNCH_ANGLE = 10;  // 사출 95 10
const uint8_t DEPLOY_LOCK_ANGLE = 10;   // 유지 95 10


//imu고장 판단

bool isOMGimu(const ImuData& imu);
//Baro 고장 판단

bool isOMGbaro(const BaroData& baro);

void resetDecisionCounters(JudgeCounters& jc);



// // ========================
bool isConnectOrDeteached(int connectPin);  //분리되면 참으로 판단

bool isAccelOver(const ImuData& imu);

bool isAltitudeUp(const BaroData& baro);

bool isAltitudeDown(const BaroData& baro);

bool isPowered(bool accelOver, bool altitudeUp, JudgeCounters& jc);  //카운터 초기화 기능 추가

bool isMotorOver(bool isPoweredNow, JudgeCounters& jc);  //카운터 초기화 추가

bool isApogee(bool altitudeUp, JudgeCounters& jc);  //상태 진입 시 카운터 초기화









void initParachuteDeploy();        //서보모터 초기화 함수
void applyParachuteDeployState();  //상태 실행함수

// //================업데이트함수==========================//

void updateFlightState(FlightData& flight, bool startFlight, bool powered, bool motorOver, bool apogee, bool descent, JudgeCounters& jc);
const char* getStateName(FlightState state);


#endif
