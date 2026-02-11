#ifndef PARACHUTE_H
#define PARACHUTE_H

#include <Arduino.h>
#include "flightType.h"

bool isConnectOrDeteached(int connectPin);
bool isAccelOver(const ImuData& imu);
bool isPressureDown(const BaroData& baro);
bool isStartFlight(bool pinDetached, bool accelOver);
bool isPowered(bool accelOver, bool pressureDown);
bool isMotorOver(bool isPoweredNow);
bool isApogee(bool pressureDown);

void updateFlightState(FlightData &flight, bool startFlight, bool powered, bool motorOver, bool apogee);
const char* getStateName(FlightState state);

#endif
