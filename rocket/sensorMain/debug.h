#pragma once

#define DEBUG 1

#if DEBUG
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#define debugVar(x) \
  Serial.print(#x); \
  Serial.print("="); \
  Serial.println(x)
#else
#define debug(x)
#define debugln(x)
#define debugVar(x)
#endif