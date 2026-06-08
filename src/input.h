#pragma once

void inputInit();
void handleButton();
void handleRotary();
void uiTick();             // services confirm / printing timeouts
void handleSleepInput();   // lightweight poll used in sleep/shutdown states
