#ifndef LORA_H
#define LORA_H

#include <Arduino.h>
#include "flightType.h"

extern bool g_parachuteDeployed;
extern bool reset;

// LoRa 초기화
void initLora();

// FlightData를 LoRa로 송신
void sendLoraFromFlight(const FlightData& f, bool g_parachuteDeployed, bool pinDetached, bool ejectBtnClicked = false,
                        bool soundBtnClicked = false,
                        bool chuteByEmergency = false,
                        bool chuteByDescent = false,
                        bool chuteByTimer = false,
                        uint8_t qIndex = 0,
                        float filterRoll = 0);

void handleLoraRxCommand();


#endif
