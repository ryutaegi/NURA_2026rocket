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
                        bool extra1 = false,
                        bool chuteByDescent = false,
                        bool chuteByTimer = false);

void handleLoraRxCommand();


#endif
