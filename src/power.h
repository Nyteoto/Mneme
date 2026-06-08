#pragma once

void  powerInit();
void  powerTick();              // call each active loop iteration
float readBatteryVoltage();
int   batteryPercent(float v);
void  updateBatteryLEDs();
void  enterSleep();
void  enterShutdown();
void  wakeUp();
void  enterDormant();           // true hardware dormant via Pico SDK
void  checkAutoSleep();
void  checkEmergencyShutdown();
