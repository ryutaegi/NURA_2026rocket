#include "pin.h"

// ======================= IMU 설정 =======================
ICM_20948_I2C imu;

// ======================= 사용자 설정 =======================
const uint32_t PRINT_PERIOD_MS = 50;
const float LPF_K = 0.20f;
const float MAG_DECLINATION_DEG = 8.6f;

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
float roll_est = 0, pitch_est = 0, yaw_est = 0;

float wGyro_f = 0.0f;

uint32_t lastMicros = 0;

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

    if (!imu.begin(WIRE_PORT, AD0_VAL)) {
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

static inline float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
static inline float clamp01(float x) { return clampf(x, 0.0f, 1.0f); }
static inline float clamp02(float x) { return clampf(x, 0.0f, 0.98f); }

static inline float wrapPi(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

static inline float wrap360(float deg) {
    while (deg < 0.0f) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    return deg;
}

static inline float blendAngleRad(float pred, float meas, float wMeas) {
    float e = wrapPi(meas - pred);
    return wrapPi(pred + wMeas * e);
}

// ======================= 축 매핑 =======================
static inline float ACC_X() { return imu.accX(); }
static inline float ACC_Y() { return imu.accY(); }
static inline float ACC_Z() { return imu.accZ(); }

static inline float GYR_X_DPS() { return imu.gyrX(); }
static inline float GYR_Y_DPS() { return imu.gyrY(); }
static inline float GYR_Z_DPS() { return imu.gyrZ(); }

static inline float MAG_X() { return imu.magX(); }
static inline float MAG_Y() { return imu.magY(); }
static inline float MAG_Z() { return imu.magZ(); }

// ======================= 계산 함수 =======================
static void computeRollPitchFromAccel(float axn, float ayn, float azn,
    float& roll, float& pitch)
{
    roll = atan2f(ayn, azn);
    pitch = atan2f(-axn, sqrtf(ayn * ayn + azn * azn));
}

static bool computeYawFromMagTiltComp(float mx, float my, float mz,
    float roll, float pitch,
    float& yaw)
{
    float mraw[3] = { mx - MAG_BIAS_X, my - MAG_BIAS_Y, mz - MAG_BIAS_Z };
    float mc[3] = { 0,0,0 };
    for (int i = 0; i < 3; i++) {
        mc[i] = MAG_AINV[i][0] * mraw[0] + MAG_AINV[i][1] * mraw[1] + MAG_AINV[i][2] * mraw[2];
    }
    float mx_c = mc[0], my_c = mc[1], mz_c = mc[2];

    float mn = sqrtf(mx_c * mx_c + my_c * my_c + mz_c * mz_c);
    if (mn < 1e-6f) return false;

    float cr = cosf(roll), sr = sinf(roll);
    float cp = cosf(pitch), sp = sinf(pitch);

    float Xh = mx_c * cp + mz_c * sp;
    float Yh = mx_c * sr * sp + my_c * cr - mz_c * sr * cp;

    yaw = atan2f(Yh, Xh);
    yaw += deg2rad(MAG_DECLINATION_DEG);
    yaw = wrapPi(yaw);
    return true;
}

static void computeAlphaBetaFixedXY_fromAccelYawRemoved(float axn, float ayn, float azn,
    float yaw,
    float& alpha, float& beta)
{
    float cy = cosf(yaw);
    float sy = sinf(yaw);

    float axp = axn * cy + ayn * sy;
    float ayp = -axn * sy + ayn * cy;
    float azp = azn;

    alpha = -asinf(clampf(ayp, -1.0f, 1.0f));
    beta = atan2f(axp, azp);
}

static float computeDynamicGyroWeight(float accMag_mg)
{
    const float oneG = 1000.0f;

    if (accMag_mg < 200.0f) return 1.0f;

    float dev = fabsf(accMag_mg - oneG);

    const float dead = 30.0f;
    if (dev < dead) return 0.0f;

    const float full = 300.0f;

    float t = dev / full;
    if (t > 1.0f) t = 1.0f;

    t = t * t * (3.0f - 2.0f * t);

    return t;
}

static void eulerRatesFromBodyRates(float p, float q, float r,
    float roll, float pitch,
    float& roll_dot, float& pitch_dot, float& yaw_dot)
{
    float cr = cosf(roll), sr = sinf(roll);
    float cp = cosf(pitch);
    float tp = tanf(pitch);

    if (fabsf(cp) < 1e-3f) cp = (cp >= 0 ? 1e-3f : -1e-3f);

    roll_dot = p + q * sr * tp + r * cr * tp;
    pitch_dot = q * cr - r * sr;
    yaw_dot = q * sr / cp + r * cr / cp;
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

    float accMag = sqrtf(ax_f * ax_f + ay_f * ay_f + az_f * az_f);
    float wGyro = computeDynamicGyroWeight(accMag);


    wGyro_f += W_LPF_K * (wGyro - wGyro_f);
    wGyro_f = clamp01(wGyro_f);
    wGyro = clamp02(wGyro);


    float an = sqrtf(ax_f * ax_f + ay_f * ay_f + az_f * az_f);
    if (an < 1e-6f) return;
    float axn = ax_f / an;
    float ayn = ay_f / an;
    float azn = az_f / an;

    float roll_acc = 0, pitch_acc = 0;
    computeRollPitchFromAccel(axn, ayn, azn, roll_acc, pitch_acc);

    if (!att_inited) {
        roll_est = roll_acc;
        pitch_est = pitch_acc;

        float yaw0 = 0;
        bool yaw_ok0 = computeYawFromMagTiltComp(mx_f, my_f, mz_f, roll_est, pitch_est, yaw0);
        yaw_est = yaw_ok0 ? yaw0 : 0.0f;

        att_inited = true;
    }
    else {
        float roll_dot, pitch_dot, yaw_dot;
        eulerRatesFromBodyRates(gx_f, gy_f, gz_f, roll_est, pitch_est, roll_dot, pitch_dot, yaw_dot);

        float roll_g = roll_est + roll_dot * dt;
        float pitch_g = pitch_est + pitch_dot * dt;
        float yaw_g = wrapPi(yaw_est + yaw_dot * dt);

        roll_est = wGyro_f * roll_g + (1.0f - wGyro_f) * roll_acc;
        pitch_est = wGyro_f * pitch_g + (1.0f - wGyro_f) * pitch_acc;

        float yaw_mag = 0;
        bool yaw_ok = computeYawFromMagTiltComp(mx_f, my_f, mz_f, roll_est, pitch_est, yaw_mag);

        if (yaw_ok) {
            yaw_est = blendAngleRad(yaw_g, yaw_mag, (1.0f - wGyro_f));
        }
        else {
            yaw_est = yaw_g;
        }
    }

    float upx = -sinf(pitch_est);
    float upy = sinf(roll_est) * cosf(pitch_est);
    float upz = cosf(roll_est) * cosf(pitch_est);

    

    imuData.ax = ax_f;  // 저장소는 m/s^2
    imuData.ay = ay_f;
    imuData.az = az_f;
    
    imuData.gx = rad2deg(gx_f);  // 라디안을 도(deg)로 변환
    imuData.gy = rad2deg(gy_f);
    imuData.gz = rad2deg(gz_f);

   

    flightData.imu = imuData;


    // flightData.roll = rad2deg(roll_est);
    // flightData.pitch = rad2deg(pitch_est);
    // flightData.yaw = rad2deg(yaw_est);
    flightData.filterRoll = rad2deg(yaw_est);

    float alphaAng = 0, betaAng = 0;
    computeAlphaBetaFixedXY_fromAccelYawRemoved(upx, upy, upz, yaw_est, alphaAng, betaAng);

    float gammaAng = yaw_est;

    flightData.roll = rad2deg(alphaAng); //alpha_d
    flightData.pitch = rad2deg(betaAng); //beta_d
    flightData.yaw = wrap360(rad2deg(gammaAng)); //gamma_d

    uint32_t nowMs = millis();
    if (nowMs - lastPrint >= PRINT_PERIOD_MS) {
        lastPrint = nowMs;

        Serial.print(flightData.roll, 2); Serial.print(F(","));
        Serial.print(flightData.pitch, 2);  Serial.print(F(","));
        Serial.println(flightData.yaw, 2);
        //Serial.print(accMag); Serial.print(F(","));
        //Serial.println(wGyro);
    }
}