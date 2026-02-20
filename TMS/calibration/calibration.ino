#include "HX711.h"

// ---------------- 핀 ----------------
#define HX_DOUT 3
#define HX_SCK  2

HX711 scale;

// ---------------- 설정 ----------------
float knownWeightKg = 1.000;   // 올릴 기준 추 무게 (kg 단위)
int samples = 20;              // 평균 샘플 수

// =================================================
long readAverage(int n)
{
long sum = 0;
for(int i=0;i<n;i++)
{
while(!scale.is_ready());
sum += scale.read();
}
return sum / n;
}

// =================================================
void setup()
{
Serial.begin(115200);
delay(2000);

Serial.println("=== HX711 CALIBRATION START ===");

scale.begin(HX_DOUT, HX_SCK);

// ---------------- 영점 ----------------
Serial.println("Remove all weight.");
Serial.println("Press any key...");
while(!Serial.available());
Serial.read();

long zero = readAverage(samples);
Serial.print("Zero raw = ");
Serial.println(zero);

delay(1000);

// ---------------- 기준추 ----------------
Serial.print("Place weight: ");
Serial.print(knownWeightKg);
Serial.println(" kg");
Serial.println("Press any key...");
while(!Serial.available());
Serial.read();

long load = readAverage(samples);
Serial.print("Load raw = ");
Serial.println(load);

// ---------------- 계산 ----------------
long diff = load - zero;

Serial.print("Raw difference = ");
Serial.println(diff);

float calibration = (float)diff / knownWeightKg;

Serial.println("============== RESULT ==============");
Serial.print("CALIBRATION = ");
Serial.println(calibration, 3);
Serial.println("Use this value in your code.");
Serial.println("====================================");
}

void loop(){}