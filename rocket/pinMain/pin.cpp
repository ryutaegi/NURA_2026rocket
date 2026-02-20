#include "pin.h"
#include "Adafruit_AHRS_Mahony.h"
#include "Adafruit_AHRS_Madgwick.h"

Adafruit_Mahony mahony; 
Adafruit_Madgwick mss;
// ======================= 자이로 캘리브레이션 변수 =======================
static bool gyro_calibrating = false;
static float gx_bias = 0.5665f, gy_bias = -0.9579f, gz_bias = 0.0773f;
static float gx_sum = 0.0f, gy_sum = 0.0f, gz_sum = 0.0f;
static uint32_t gyro_sample_count = 0;
const uint32_t GYRO_CAL_SAMPLES = 30000;


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


// ======================= IMU 초기화 =======================
bool initializeIMU()
{
   
    WIRE_PORT.begin();
     
    delay(200);
   
    return true;
}

// ======================= 유틸 =======================
static inline float deg2rad(float d) { return d * (PI / 180.0f); }
static inline float rad2deg(float r) { return r * (180.0f / PI); }

// ======================= 축 매핑 =======================
static inline float ACC_X() { return myICM.accX(); }
static inline float ACC_Y() { return myICM.accY(); }
static inline float ACC_Z() { return myICM.accZ(); }

// ======================= 축 매핑 (바이어스 적용) =======================
static inline float GYR_X_DPS() { return myICM.gyrX() - gx_bias; }
static inline float GYR_Y_DPS() { return myICM.gyrY() - gy_bias; }
static inline float GYR_Z_DPS() { return myICM.gyrZ() - gz_bias; }


static inline float MAG_X() { return myICM.magX(); }
static inline float MAG_Y() { return myICM.magY(); }
static inline float MAG_Z() { return myICM.magZ(); }

// ======================= 계산 함수 =======================
/* void calculateEulerAngles(long q1_raw, long q2_raw, long q3_raw, float& roll, float& pitch, float& yaw) {

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
}*/


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

    imuData.gx = rad2deg(gx);  // 라디안을 도(deg)로 변환
    imuData.gy = rad2deg(gy);
    imuData.gz = rad2deg(gz);

    
    mahony.updateIMU(gx, gy, gz, ax, ay, az, dt);
    mahony.computeAngles();
    earth_roll  = mahony.roll  ;   // rad → deg
    earth_pitch = mahony.pitch ;
    earth_yaw   = mahony.yaw   ;
  
  Serial.print(-100); 
    Serial.print(",");//Serial.println(F("//"));
    Serial.print(100); //Serial.println(F("//"));
    Serial.print(","); //Serial.println(F("//"));
    Serial.print(earth_yaw, 2);Serial.print("//"); //Serial.println(F("//"));
     Serial.print(imuData.gx);  Serial.print("//");
  Serial.print(imuData.gy); Serial.print("//");
  Serial.println(imuData.gz); 
    //Serial.print(earth_roll, 2); Serial.print(F("//"));
    //Serial.print(earth_pitch, 2);  Serial.print(F("//"));
    //Serial.print(earth_yaw, 2); Serial.println(F("//"));
/*
    mahony.update(gx, gy, gz, ax, ay, az, mx, my, mz, dt);
    mahony.computeAngles();
    
    earth_roll  = mahony.roll  ;   // rad → deg
    earth_pitch = mahony.pitch ;
    earth_yaw   = mahony.yaw   ;
  
  */

    // Earth 각도 추출
   

 
    
   // Serial.print(earth_roll, 2); Serial.print(F("//"));
    //Serial.print(earth_pitch, 2);  Serial.print(F("//"));
    // ======================= 자이로 바이어스 측정 =======================
if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input == "cal") {  // 시리얼 모니터에 "cal" 입력
        Serial.println("자이로 바이어스측정");
        gyro_calibrating = true;
        gyro_sample_count = 0;
        gx_sum = 0.0f; gy_sum = 0.0f; gz_sum = 0.0f;
    }
}

if (gyro_calibrating) {
    float gx_raw = GYR_X_DPS();
    float gy_raw = GYR_Y_DPS();
    float gz_raw = GYR_Z_DPS();
    
    gx_sum += gx_raw;
    gy_sum += gy_raw;
    gz_sum += gz_raw;
    
    gyro_sample_count++;
    
    if (gyro_sample_count >= GYRO_CAL_SAMPLES) {
        gx_bias = gx_sum / GYRO_CAL_SAMPLES;
        gy_bias = gy_sum / GYRO_CAL_SAMPLES;
        gz_bias = gz_sum / GYRO_CAL_SAMPLES;
        
        Serial.println("=== 자이로 캘리브레이션 완료 ===");
        Serial.print("GX Bias: "); Serial.println(gx_bias, 4);
        Serial.print("GY Bias: "); Serial.println(gy_bias, 4);
        Serial.print("GZ Bias: "); Serial.println(gz_bias, 4);
        Serial.println("이제 이 값을 사용해 자이로 데이터를 보정하세요!");
        
        gyro_calibrating = false;
    }
    return;  // 캘리브레이션 중에는 일반 IMU 처리 스킵
}




    // flightData에 저장
    flightData.roll  = earth_roll;
    flightData.pitch = earth_pitch;
    flightData.yaw   = earth_yaw;

/*
    icm_20948_DMP_data_t data;
    myICM.readDMPdataFromFIFO(&data);

    /* if ((myICM.status == ICM_20948_Stat_Ok) || (myICM.status == ICM_20948_Stat_FIFOMoreDataAvail))
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
*/
    flightData.imu = imuData;


    uint32_t nowMs = millis();
    if (nowMs - lastPrint >= PRINT_PERIOD_MS) {
        lastPrint = nowMs;

       // Serial.print(flightData.roll, 2); Serial.print(F("//"));
       // Serial.print(flightData.pitch, 2);  Serial.print(F("//"));
       // Serial.print(flightData.yaw, 2); Serial.print(F("//"));
       // Serial.println(flightData.filterRoll,2); 
        //Serial.println(wGyro);
    }
    

}