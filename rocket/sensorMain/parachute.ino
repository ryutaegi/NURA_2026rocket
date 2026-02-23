#include "parachute.h"
// //낙하산 코드 시작

// //imu고장 판단

bool isOMGimu(const ImuData& imu){
  return (imu.ax == 100) || (imu.ay == 100) || (imu.az == 100);
}
//Baro 고장 판단

bool isOMGbaro(const BaroData& baro) {
  return (baro.pressure < 100 || baro.pressure > 1200);
}

void resetDecisionCounters(JudgeCounters& jc)  // 이상치 발견 시 상태 변경할 때 모든 누적값 초기화
{
  jc.powered = 0;
  jc.motorOver = 0;
  jc.apogee = 0;
  jc.descent = 0;
}

bool isConnectOrDeteached(int connectPin)  //분리되면 참으로 판단
{
  // LOW -> 연결됨
  // HIGH -> 분리됨
  return (digitalRead(connectPin) == HIGH);
}

bool isAccelOver(const ImuData& imu) {  //제곱값 비교로 바꿈
  const float G = 9.81;
  const float THRESHOLD_SQ = (1.1 * G) * (1.1 * G);  //임계값은 적절하게 조정하기
  float magSq = imu.ax * imu.ax + imu.ay * imu.ay + imu.az * imu.az;
  return magSq >= THRESHOLD_SQ;
}

bool isAltitudeUp(const BaroData& baro) {
  //static int countU = 0;
  static float prevU = 0;

  if(fabs(prevU - baro.climbRate) > 0.05f && launchTimeStarted) {
    // Serial.print(flight.baro.climbRate);
    // Serial.print(" ");
    // Serial.println(prevU);
    if(flight.baro.climbRate > 0.2) //상승 시 카운트 +1
      {jc.countU++;
      //Serial.println(countU);
      }
    else{
      if(jc.countU > 0) //하락중이면 count가 0이상일 때만 count 1 감소
      jc.countU-=1;
    }
    prevU = flight.baro.climbRate;
    }
  if(jc.countU > 50)
  return true;
  else
  return false;
}

// bool isAltitudeUp(const BaroData& baro) {
//   static int countU = 0;

//   if(!launchTimeStarted) return false;

//     if(flight.baro.climbRate > 0.2) //상승 시 카운트 +1
//       {
//         countU++;
//       }
//     else{
//       if(countU > 0) //하락중이면 count가 0이상일 때만 count 1 감소
//       countU-=2;
//     }
    
//   if(countU > 20)
//   return true;
//   else
//   return false;
// }

bool isAltitudeDown(const BaroData& baro) {
  static float prevD = 0.0f;
  //static int countD = 0;

  if(fabs(prevD - baro.climbRate) > 0.05f && launchTimeStarted) {
    if(flight.baro.climbRate < 0.2) //하강 시 카운트 +1
      jc.countD++;
    else{
      if(jc.countD > 0) //하락중이면 count가 0이상일 때만 count 1 감소
      jc.countD-=1;
    }
    prevD = flight.baro.climbRate;
    }
  if(jc.countD > 50)
  return true;
  else
  return false;
}

bool isPowered(bool accelOver, bool altitudeUp, JudgeCounters& jc)  //카운터 초기화 기능 추가
{
  const uint8_t THRESHOLD = 10;  // 10Hz 기준 ≈ 1초

  if (accelOver && altitudeUp) {
    if (jc.powered < THRESHOLD) jc.powered++;
  } else {
    jc.powered = 0;
  }

  return jc.powered >= THRESHOLD;
}

bool isMotorOver(bool isPoweredNow, JudgeCounters& jc)  //카운터 초기화 추가
{
  const uint8_t THRESHOLD = 20;  // 10Hz 기준 ≈ 1초

  if (!isPoweredNow) {
    if (jc.motorOver < THRESHOLD) jc.motorOver++;
  } else {
    jc.motorOver = 0;
  }

  return jc.motorOver >= THRESHOLD;
}



void initParachuteDeploy()  //서보모터 초기화 함수
{
  deployServo.attach(PIN_DEPLOY_SERVO);
  deployServo.write(DEPLOY_ARM_ANGLE);

  deployCtl.state = DEPLOY_IDLE;
  deployCtl.deployed = false;
}

void applyParachuteDeployState()  //상태 실행함수
{
  if (deployCtl.deployed) return;

  switch (deployCtl.state) {

    case DEPLOY_IDLE:
      deployServo.write(DEPLOY_ARM_ANGLE);
      break;

    case DEPLOY_PUNCH:
      deployServo.write(DEPLOY_PUNCH_ANGLE);
      break;

    case DEPLOY_LOCK:
      deployServo.write(DEPLOY_LOCK_ANGLE);
      break;

    case DEPLOY_DONE:
      deployCtl.deployed = true;
      // deployServo.detach(); // 선택
      break;
  }
}

//================업데이트함수==========================//

void updateFlightState(FlightData& flight, bool startFlight, bool powered, bool motorOver, bool apogee, bool descent, JudgeCounters& jc)
// !altitudeUp 누적 → 이벤트
//bool descent,    // altitudeDown OR !accelOver 누적 → 상태
//JudgeCounters &jc
{
  switch (flight.state) {

    case STANDBY:
      if (startFlight) {
        flight.state = LAUNCHED;

        // 🔴 초기화: 이전 실험/노이즈 완전 제거
        jc = {};  // 모든 카운터 0으로

        Serial.println("STANDBY → LAUNCHED");
      }
      break;

    case LAUNCHED:
      if (powered) {
        flight.state = POWERED;

        // 🔴 추력 시작 시, 추력 종료 카운터 무효화
        jc.motorOver = 0;

        Serial.println("LAUNCHED → POWERED");
      }
      break;

    case POWERED:
      if (motorOver) {
        flight.state = COASTING;

        // 🔴 이제부터 APOGEE만 의미 있음
        jc.apogee = 0;
        jc.countU = 0;

        Serial.println("POWERED → COASTING");
      }
      break;

    case COASTING:
      // APOGEE는 "상승 종료 이벤트"
      if (apogee) {
        flight.state = APOGEE;

        // 🔴 DESCENT는 APOGEE 이후부터 카운트
        jc.descent = 0;
        jc.countD = 0;

        Serial.println("COASTING → APOGEE");
      }
      break;

    case APOGEE:
      // DESCENT는 "하강 상태 확정"
      if (descent) {
        flight.state = DESCENT;

        Serial.println("APOGEE → DESCENT");

        // 🔴 낙하산 사출 트리거 (DESCENT 진입 시 단 1회)
        if (!deployCtl.deployed) {
          deployCtl.state = DEPLOY_PUNCH;
          g_parachuteDeployed = true;

          Serial.print("B->A sent parachute=");
          Serial.print(g_parachuteDeployed);
          Serial.print(" t=");
          Serial.println(millis());
        }
      }
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
