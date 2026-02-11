#pragma once

#include <Arduino.h>  
#include <ICM_20948.h>

// ======================= IMU 설정 =======================
#define WIRE_PORT Wire
#define AD0_VAL 1

extern ICM_20948_I2C imu;

struct ImuData{
float ax;  // 가속도 X (m/s^2)
float ay;  // 가속도 Y
float az;  // 가속도 Z

float gx;  // 각속도 X (deg/s)
float gy;  // 각속도 Y
float gz;  // 각속도 Z
}; 

struct FlightData {
    // 원시 센서 데이터
    ImuData imu;         // IMU 센서 데이터
    // 자세 추정 (지상좌표계 기준)
    float roll;          // 롤 각도 (deg)
    float pitch;         // 피치 각도 (deg)
    float yaw;           // 요 각도 (deg)
    // 상보필터 보정 (요 각도 기준)
    float filterRoll; 
    float servoDegree;  // 상보필터로 보정한 롤 각도 (deg)
    uint32_t timeMs;
};
// ======================= 사용자 설정 =======================


extern const uint32_t PRINT_PERIOD_MS;
extern const float LPF_K;
extern const float MAG_DECLINATION_DEG;
extern const float MAG_BIAS_X;
extern const float MAG_BIAS_Y;
extern const float MAG_BIAS_Z;
extern const float MAG_AINV[3][3];
extern const float ACC_1G;
extern const float W_LPF_K;

// ======================= 내부 변수 =======================
extern float ax_f, ay_f, az_f;
extern float gx_f, gy_f, gz_f;
extern float mx_f, my_f, mz_f;

extern ImuData imuData;
extern FlightData flightData;  

extern uint32_t lastPrint;
extern bool att_inited;
extern float roll_est, pitch_est, yaw_est;
extern float wGyro_f;
extern uint32_t lastMicros;

// ======================= 함수 프로토타입 =======================
// 초기화
void scanI2C(void);                   
bool initializeIMU(void);              

// 메인
void processIMU(void);

// 유틸
float deg2rad(float d);               
float rad2deg(float r);               
float clampf(float x, float lo, float hi);  
float clamp01(float x);               
float clamp02(float x);                
float wrapPi(float a);                
float wrap360(float deg);             
float blendAngleRad(float pred, float meas, float wMeas);  

// 센서 데이터 접근
float ACC_X(void);                     
float ACC_Y(void);                     
float ACC_Z(void);                    
float GYR_X_DPS(void);                 
float GYR_Y_DPS(void);                
float GYR_Z_DPS(void);                 
float MAG_X(void);                    
float MAG_Y(void);                     
float MAG_Z(void);                    

// 계산 함수
void computeRollPitchFromAccel(float axn, float ayn, float azn, float& roll, float& pitch);  
bool computeYawFromMagTiltComp(float mx, float my, float mz, float roll, float pitch, float& yaw);  
void computeAlphaBetaFixedXY_fromAccelYawRemoved(float axn, float ayn, float azn, float yaw, float& alpha, float& beta); 
float computeDynamicGyroWeight(float accMag_mg);  
void eulerRatesFromBodyRates(float p, float q, float r, float roll, float pitch, float& roll_dot, float& pitch_dot, float& yaw_dot);  


