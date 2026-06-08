#pragma once

void     audioShutdown();
void     playTone(int freq, int durationMs, float volume);
void     playStartupChime();
void     playLevelUpChime();
void     playTaskCompleteDing();
void     playSlowChime();
bool     audioIsActive();
