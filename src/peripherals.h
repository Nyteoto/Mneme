#pragma once
// Peripheral driver objects — defined once in main.cpp, extern everywhere else.
#include <Adafruit_SSD1306.h>
#include <Adafruit_MCP23X17.h>
#include "RTClib.h"

extern Adafruit_SSD1306  display;
extern Adafruit_MCP23X17 mcp;
extern RTC_DS1307        rtc;
