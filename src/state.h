#pragma once
#include "config.h"
#include <stdint.h>
#include <Arduino.h>   // HIGH, LOW, etc.

// ── Full calendar date ─────────────────────────────────────────────────────
// Storing year+month+day avoids the month-boundary modulo bug in the original
// code where (day - lastDay + 31) % 31 gave wrong results at month end.
struct SavedDate {
    int16_t day   = -1;
    int16_t month = -1;
    int16_t year  = -1;

    bool valid() const { return year > 0; }
    bool operator==(const SavedDate& o) const {
        return day == o.day && month == o.month && year == o.year;
    }
    bool operator!=(const SavedDate& o) const { return !(*this == o); }

    // Returns true if advancing this date by one calendar day equals 'next'.
    // Used to decide whether a heatmap streak is unbroken.
    bool isYesterdayOf(const SavedDate& next) const;
};

// ── Sub-state groups ───────────────────────────────────────────────────────
struct GameState {
    int32_t  level    = 1;
    float    exp      = 0.0f;
    float    expReq   = 10.0f;
    uint32_t btnCount = 0;
};

struct HeatmapState {
    SavedDate lastDate;
    int16_t   streak      = 0;
    bool      grid[HEATMAP_CELLS] = {};
    bool      loggedToday = false;
    bool      flashActive = false;
    uint32_t  flashStart  = 0;
};

struct TaskInitState {
    uint32_t startTime = 0;
    bool     running   = false;
    bool     finished  = false;
};

struct TaskPrioState {
    uint32_t startTime    = 0;
    bool     running      = false;
    uint32_t logs[3]      = {};
    int16_t  logCount     = 0;
    bool     firstUseToday = true;
};

struct EvalState {
    TaskEvalState mode      = EVAL_INACTIVE;
    uint32_t      holdStart = 0;
    uint32_t      dispStart = 0;
    uint8_t       msgIndex  = 0;
};

struct PowerMgr {
    DeviceState device              = STATE_ACTIVE;
    PowerSource source              = POWER_BATTERY;
    float       stableV             = BATT_FULL_V;
    int16_t     stablePct           = 100;
    uint32_t    lastActivity        = 0;
    uint32_t    sleepStart          = 0;
    uint32_t    lastSrcCheck        = 0;
    bool        flashActive           = false;
    uint32_t    flashStart            = 0;
    bool        ignoreNextWakeRelease = false;
    float       filteredBattV         = 0.0f;  // EMA-smoothed voltage, 0 = not yet seeded
};

// ── Master application state ───────────────────────────────────────────────
struct AppState {
    GameState    game;
    HeatmapState heatmap;
    TaskInitState taskInit;
    TaskPrioState taskPrio;
    EvalState    eval;
    PowerMgr     pwr;

    ScreenMode screen       = SCREEN_HOME;
    int  lastRotaryPos      = -1;
    int  lastRotaryIdx      = -1;
    bool lastBtn            = HIGH;
    uint32_t btnPressStart  = 0;
    bool btnHeld            = false;
    float volume            = 0.2f;

    bool     levelUpActive  = false;
    uint32_t levelUpStart   = 0;

    bool     swLimitNotif      = false;
    uint32_t swLimitNotifStart = 0;

    SavedDate today;       // refreshed each loop from RTC
    SavedDate savedDate;   // last-known date persisted in EEPROM

    bool     dirty        = false;  // true when EEPROM flush is needed
    uint32_t lastSaveTime = 0;
};

extern AppState app;
