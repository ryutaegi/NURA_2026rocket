#include "HX711.h"
#include <SPI.h>
#include <SdFat.h>

// ---------------- 핀 ----------------
#define HX_DOUT 3
#define HX_SCK  2
#define SD_CS   10

// ---------------- 보정값 ----------------
const float CALIBRATION = -21000.0f;

// ---------------- 객체 ----------------
HX711 scale;
SdFat SD;
SdFile file;

uint32_t lastFlush = 0;

// =================================================
void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("BOOT"));

  scale.begin(HX_DOUT, HX_SCK);

  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD FAIL"));
    while(1);
  }
  Serial.println(F("SD OK"));

  // TEXT 파일 생성
  if (!file.open("tms.txt", O_WRITE | O_CREAT | O_TRUNC)) {
    Serial.println(F("FILE FAIL"));
    while(1);
  }
  Serial.println(F("FILE OK"));

  // 헤더 작성 (엑셀 편하게)
  file.println(F("time_ms,raw"));
}

// =================================================
void loop()
{
  if (!scale.is_ready()) return;

  uint32_t t = millis();
  int32_t raw = scale.read();

  // ---------- SD 텍스트 저장 ----------
  file.print(t);
  file.print(',');
  file.println(raw);

  // ---------- 시리얼 출력 ----------
  float force = raw / CALIBRATION;

  Serial.print(t);
  Serial.print(',');
  Serial.print(raw);
  Serial.print(',');
  Serial.println(force);

  // ---------- flush 보호 ----------
  if (millis() - lastFlush > 1000) {
    file.flush();
    lastFlush = millis();
  }
}

