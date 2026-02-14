#include "pin.h"

// ======================= IMU 설정 =======================
ICM_20948_I2C myICM;

// ======================= 사용자 설정 =======================
const uint32_t PRINT_PERIOD_MS = 50;
const float LPF_K = 0.20f;

ImuData imuData = {};
FlightData flightData = {};

const float MAG_BIAS_X = 0.0f;
const float MAG_BIAS_Y = 0.0f;
const float MAG_BIAS_Z = 0.0f;

const float MAG_AINV[3][3] = {
  {1.0f, 0.0f, 0.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 0.0f, 1.0f}
};

const float ACC_1G = 1.0f;
const float W_LPF_K = 0.15f;

// ======================= 내부 변수 =======================
float ax_f = 0, ay_f = 0, az_f = 0;
float gx_f = 0, gy_f = 0, gz_f = 0;
float mx_f = 0, my_f = 0, mz_f = 0;

uint32_t lastPrint = 0;

bool att_inited = false;

float wGyro_f = 0.0f;

uint32_t lastMicros = 0;

float earth_roll = 0.0f;
float earth_pitch = 0.0f;
float earth_yaw = 0.0f;

// ======================= I2C 스캔 함수 =======================
void scanI2C() {
    byte error, address;
    int nDevices = 0;

    Serial.println("\n→ I2C 장치 스캔 중...");

    for (address = 1; address < 127; address++) {
        WIRE_PORT.beginTransmission(address);
        error = WIRE_PORT.endTransmission();

        if (error == 0) {
            Serial.print("  I2C 장치 발견: 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
            nDevices++;
        }
    }

    if (nDevices == 0) {
        Serial.println("  I2C 장치 없음! 연결 확인하세요.");
    }
    else {
        Serial.print("  총 ");
        Serial.print(nDevices);
        Serial.println("개 장치 발견");
    }
}

// ======================= IMU 초기화 =======================
bool initializeIMU()
{
    Serial.println("\n  [1] I2C 초기화 중...");
    WIRE_PORT.begin();
    delay(200);
    Serial.println("      → I2C 초기화 완료");

    Serial.print("  [2] ICM_20948 탐색 중 (AD0=");
    Serial.print(AD0_VAL);
    Serial.println(")...");

    if (!myICM.begin(WIRE_PORT, AD0_VAL)) {
        Serial.println("      → 실패! I2C 스캔 결과:");
        scanI2C();
        return false;
    }

    Serial.println("      → ICM_20948 발견됨");
    return true;
}

// ======================= 유틸 =======================
static inline float deg2rad(float d) { return d * (PI / 180.0f); }
static inline float rad2deg(float r) { return r * (180.0f / PI); }

// ======================= 축 매핑 =======================
static inline float ACC_X() { return myICM.accX(); }
static inline float ACC_Y() { return myICM.accY(); }
static inline float ACC_Z() { return myICM.accZ(); }

static inline float GYR_X_DPS() { return myICM.gyrX(); }
static inline float GYR_Y_DPS() { return myICM.gyrY(); }
static inline float GYR_Z_DPS() { return myICM.gyrZ(); }

static inline float MAG_X() { return myICM.magX(); }
static inline float MAG_Y() { return myICM.magY(); }
static inline float MAG_Z() { return myICM.magZ(); }

// ======================= 계산 함수 =======================
void calculateEulerAngles(long q1_raw, long q2_raw, long q3_raw, float& roll, float& pitch, float& yaw) {

    double q1 = ((double)q1_raw) / 1073741824.0;
    double q2 = ((double)q2_raw) / 1073741824.0;
    double q3 = ((double)q3_raw) / 1073741824.0;
    double q0 = sqrt(1.0 - ((q1 * q1) + (q2 * q2) + (q3 * q3)));

    double qw = q0;
    double qx = q2;
    double qy = q1;
    double qz = -q3;

    // Roll
    double t0 = +2.0 * (qw * qx + qy * qz);
    double t1 = +1.0 - 2.0 * (qx * qx + qy * qy);
    roll = atan2(t0, t1) * 180.0 / PI;

    // Pitch
    double t2 = +2.0 * (qw * qy - qx * qz);
    t2 = t2 > 1.0 ? 1.0 : t2;
    t2 = t2 < -1.0 ? -1.0 : t2;
    pitch = asin(t2) * 180.0 / PI;

    // Yaw
    double t3 = +2.0 * (qw * qz + qx * qy);
    double t4 = +1.0 - 2.0 * (qy * qy + qz * qz);
    yaw = atan2(t3, t4) * 180.0 / PI;
}


// ======================= 메인 IMU 처리 =======================
void processIMU()
{
    uint32_t nowUs = micros();
    float dt = (nowUs - lastMicros) * 1e-6f;
    lastMicros = nowUs;
    if (dt <= 0.0f || dt > 0.2f) return;

    float ax = ACC_X();
    float ay = ACC_Y();
    float az = ACC_Z();

    float gx = deg2rad(GYR_X_DPS());
    float gy = deg2rad(GYR_Y_DPS());
    float gz = deg2rad(GYR_Z_DPS());

    float mx = MAG_X();
    float my = -MAG_Y();
    float mz = -MAG_Z();

    ax_f += LPF_K * (ax - ax_f);
    ay_f += LPF_K * (ay - ay_f);
    az_f += LPF_K * (az - az_f);

    gx_f += LPF_K * (gx - gx_f);
    gy_f += LPF_K * (gy - gy_f);
    gz_f += LPF_K * (gz - gz_f);

    mx_f += LPF_K * (mx - mx_f);
    my_f += LPF_K * (my - my_f);
    mz_f += LPF_K * (mz - mz_f);

    imuData.ax = ax_f;  // 저장소는 m/s^2
    imuData.ay = ay_f;
    imuData.az = az_f;

    imuData.gx = rad2deg(gx_f);  // 라디안을 도(deg)로 변환
    imuData.gy = rad2deg(gy_f);
    imuData.gz = rad2deg(gz_f);

    icm_20948_DMP_data_t data;
    myICM.readDMPdataFromFIFO(&data);

    if ((myICM.status == ICM_20948_Stat_Ok) || (myICM.status == ICM_20948_Stat_FIFOMoreDataAvail))
    {
        // --------------------------------------------------
        // 1. 6축 데이터 (Game Rotation Vector)
        // --------------------------------------------------
        if ((data.header & DMP_header_bitmap_Quat6) > 0)
        {
            calculateEulerAngles(
                data.Quat6.Data.Q1,
                data.Quat6.Data.Q2,
                data.Quat6.Data.Q3,
                earth_roll,
                earth_pitch,
                earth_yaw
            );

            flightData.filterRoll = earth_yaw;
           
        }

        // --------------------------------------------------
        // 2. 9축 데이터 (Rotation Vector) -> flightData에 저장
        // --------------------------------------------------
        if ((data.header & DMP_header_bitmap_Quat9) > 0)
        {
           
            calculateEulerAngles(
                data.Quat9.Data.Q1,
                data.Quat9.Data.Q2,
                data.Quat9.Data.Q3,
                earth_roll,
                earth_pitch,
                earth_yaw
            );

            flightData.roll = earth_roll;
            flightData.pitch = earth_pitch;
            flightData.yaw = earth_yaw;
           
        }
    }

    if (myICM.status != ICM_20948_Stat_FIFOMoreDataAvail)
    {
        delay(10);
    }

    flightData.imu = imuData;


    uint32_t nowMs = millis();
    if (nowMs - lastPrint >= PRINT_PERIOD_MS) {
        lastPrint = nowMs;

        Serial.print(flightData.roll, 2); Serial.print(F("//"));
        Serial.print(flightData.pitch, 2);  Serial.print(F("//"));
        Serial.print(flightData.yaw, 2); Serial.print(F("//"));
        Serial.println(flightData.filterRoll,2); 
        //Serial.println(wGyro);
    }
}