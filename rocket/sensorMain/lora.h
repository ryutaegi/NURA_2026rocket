#ifndef LORA_H
#define LORA_H

#include <Arduino.h>
#include "flightType.h"

// LoRa 초기화
void initLora();

// FlightData를 LoRa로 송신
void sendLoraFromFlight(const FlightData& f, bool parachuteDeployed, uint8_t humidity = 0);

#endif
