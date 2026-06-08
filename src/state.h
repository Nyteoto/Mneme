#pragma once
#include "config.h"
#include <stdint.h>
#include <Arduino.h>   // HIGH, LOW

// ── Channel: one real-life domain, tracked as a never-resetting timeline ───
// startUnix is the first-ever press (0 = channel never used). Each press
// appends the current RTC unix time to sectionStart[], closing the previous
// bar and opening a new one. The running day-count is simply (now-startUnix);
// it never resets, because it measures elapsed life in the domain, not a streak.
struct Channel {
    uint32_t startUnix = 0;
    uint32_t sectionStart[MAX_SECTIONS] = {};
    uint32_t sectionCount = 0;            // uint32 keeps the struct padding-free
};

// ── Print configuration (persisted, edited on the Settings screen) ─────────
struct PrintConfig {
    uint8_t fontSize = 1;   // 1..3
    uint8_t bold     = 0;   // 0 / 1
    uint8_t barWidth = 4;   // chart bar thickness on the printout
    uint8_t reserved = 0;
};

// ── Power / battery management ─────────────────────────────────────────────
struct PowerMgr {
    DeviceState device                = STATE_ACTIVE;
    PowerSource source                = POWER_BATTERY;
    float       stableV               = BATT_FULL_V;
    int16_t     stablePct             = 100;
    uint32_t    lastActivity          = 0;
    uint32_t    sleepStart            = 0;
    uint32_t    lastSrcCheck          = 0;
    bool        flashActive           = false;
    uint32_t    flashStart            = 0;
    bool        ignoreNextWakeRelease = false;
    float       filteredBattV         = 0.0f;
};

// ── Master application state ───────────────────────────────────────────────
struct AppState {
    Channel     channels[NUM_CHANNELS];
    PrintConfig print;
    PowerMgr    pwr;

    // UI / navigation
    UiMode  ui              = UI_MAIN;
    uint8_t currentChannel  = 0;        // 0..NUM_CHANNELS-1, selected by rotary
    uint8_t menuSel         = MENU_PRINT;
    uint8_t settingField    = SET_FONT;
    uint32_t uiTimer          = 0;      // confirm / printing splash deadline anchor
    uint32_t sectionBlinkUntil = 0;     // button-LED pulses until this millis()

    // Input tracking
    int  lastRotaryPos      = -1;
    int  lastRotaryIdx      = -1;
    bool lastBtn            = HIGH;
    uint32_t btnPressStart  = 0;
    bool btnHeld            = false;

    float volume            = 0.2f;     // retained (amp wired) though unused for now

    bool     dirty          = false;    // true when an EEPROM flush is pending
    uint32_t lastSaveTime   = 0;
};

extern AppState app;
