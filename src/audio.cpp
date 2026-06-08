#include "audio.h"
#include "config.h"
#include "state.h"
#include <I2S.h>
#include <math.h>
#include <Arduino.h>

static I2S  i2s(OUTPUT, I2S_BCLK, I2S_DOUT);
static bool active = false;

bool audioIsActive() { return active; }

static bool initAudio() {
    if (!active) active = i2s.begin(AUDIO_SAMPLE_RATE);
    return active;
}

void audioShutdown() {
    if (!active) return;
    // Flush silence to avoid click on amplifier shutdown
    const int16_t silence = 0;
    for (int i = 0; i < 50; i++) { i2s.write(silence); i2s.write(silence); }
    i2s.end();
    active = false;
}

void playTone(int freq, int durationMs, float volume) {
    if (!initAudio()) return;
    float vol = constrain(volume, 0.0f, 0.2f) * app.volume;
    int samples = (int)((long)durationMs * AUDIO_SAMPLE_RATE / 1000);
    float phase = 0.0f;
    const float phaseInc = 2.0f * PI * freq / AUDIO_SAMPLE_RATE;
    for (int i = 0; i < samples; i++) {
        int16_t s = (int16_t)(sinf(phase) * vol * 32767.0f);
        i2s.write(s);
        i2s.write(s);
        phase += phaseInc;
        if (phase >= 2.0f * PI) phase -= 2.0f * PI;
    }
}

void playStartupChime() {
    static const int notes[] = {523, 659, 784};
    static const int durs[]  = { 80,  80, 160};
    for (int n = 0; n < 3; n++) { playTone(notes[n], durs[n], 0.2f); delay(30); }
    delay(50);
    audioShutdown();
}

void playLevelUpChime() {
    static const int notes[] = {523, 659, 784, 1047, 1319};
    static const int durs[]  = { 70,  70,  70,  100,  450};
    for (int n = 0; n < 5; n++) {
        digitalWrite(GREEN_LED,  HIGH);
        digitalWrite(BUTTON_LED, HIGH);
        playTone(notes[n], durs[n], 0.15f);
        digitalWrite(GREEN_LED,  LOW);
        digitalWrite(BUTTON_LED, LOW);
        delay(20);
    }
    delay(50);
    audioShutdown();
}

void playTaskCompleteDing() {
    playTone(1046, 200, 0.15f);
    delay(50);
    playTone(1318, 100, 0.15f);
    delay(50);
    audioShutdown();
}

void playSlowChime() {
    static const int notes[] = {440, 554, 659};
    static const int durs[]  = {200, 200, 400};
    for (int n = 0; n < 3; n++) { playTone(notes[n], durs[n], 0.12f); delay(100); }
    delay(50);
    audioShutdown();
}
