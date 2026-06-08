#include "storage.h"
#include "config.h"
#include "state.h"
#include "peripherals.h"
#include <EEPROM.h>

void storageInit() {
    EEPROM.begin(EEPROM_SIZE);

    // If the magic matches but the version is wrong, the address layout has
    // changed and every field lands at the wrong offset (producing garbage like
    // level = -51). Wipe the entire used area so loadData() starts fresh.
    uint16_t magic; uint8_t ver;
    EEPROM.get(ADDR_MAGIC,   magic);
    EEPROM.get(ADDR_VERSION, ver);
    if (magic == EEPROM_MAGIC && ver != EEPROM_VERSION) {
        for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
        EEPROM.commit();
    }
}

void saveData() {
    const uint16_t magic = EEPROM_MAGIC;
    const uint8_t  ver   = EEPROM_VERSION;
    EEPROM.put(ADDR_MAGIC,   magic);
    EEPROM.put(ADDR_VERSION, ver);

    EEPROM.put(ADDR_LEVEL,     (int32_t)app.game.level);
    EEPROM.put(ADDR_EXP,       app.game.exp);
    EEPROM.put(ADDR_EXP_REQ,   app.game.expReq);
    EEPROM.put(ADDR_BTN_COUNT, (uint32_t)app.game.btnCount);
    EEPROM.put(ADDR_VOLUME,    app.volume);

    EEPROM.put(ADDR_LAST_DAY,   app.savedDate.day);
    EEPROM.put(ADDR_LAST_MONTH, app.savedDate.month);
    EEPROM.put(ADDR_LAST_YEAR,  app.savedDate.year);

    for (int i = 0; i < 3; i++) {
        EEPROM.put(ADDR_TIMER_LOGS + i * 4, (uint32_t)app.taskPrio.logs[i]);
    }
    EEPROM.put(ADDR_TIMER_COUNT, (int16_t)app.taskPrio.logCount);

    EEPROM.put(ADDR_HEAT_DAY,   app.heatmap.lastDate.day);
    EEPROM.put(ADDR_HEAT_MONTH, app.heatmap.lastDate.month);
    EEPROM.put(ADDR_HEAT_YEAR,  app.heatmap.lastDate.year);
    EEPROM.put(ADDR_STREAK,     (int16_t)app.heatmap.streak);

    uint32_t bits = 0;
    for (int i = 0; i < HEATMAP_CELLS; i++) {
        if (app.heatmap.grid[i]) bits |= (1UL << i);
    }
    EEPROM.put(ADDR_GRID, bits);

    uint8_t lt = app.heatmap.loggedToday ? 1 : 0;
    EEPROM.put(ADDR_LOGGED_TODAY, lt);

    uint8_t  swRunning = app.taskPrio.running ? 1 : 0;
    uint32_t swElapsed = app.taskPrio.running
        ? (uint32_t)(millis() - app.taskPrio.startTime) : 0;
    uint32_t swUnix    = (uint32_t)rtc.now().unixtime();
    EEPROM.put(ADDR_SW_RUNNING,   swRunning);
    EEPROM.put(ADDR_SW_ELAPSED,   swElapsed);
    EEPROM.put(ADDR_SW_SAVE_UNIX, swUnix);

    EEPROM.commit();
    app.dirty        = false;
    app.lastSaveTime = millis();
}

void loadData() {
    uint16_t magic; uint8_t ver;
    EEPROM.get(ADDR_MAGIC,   magic);
    EEPROM.get(ADDR_VERSION, ver);

    // Reject any save whose version doesn't match the current layout.
    // When the EEPROM address map changes (as it did from v5→v6) the old bytes
    // land at wrong offsets, producing garbage values like level=-51.
    // Mismatched saves are overwritten with defaults on the next saveData() call.
    if (magic != EEPROM_MAGIC || ver != EEPROM_VERSION) return;

    int32_t lv; EEPROM.get(ADDR_LEVEL,     lv); app.game.level    = lv;
    float   ex; EEPROM.get(ADDR_EXP,        ex); app.game.exp      = ex;
    float   er; EEPROM.get(ADDR_EXP_REQ,    er); app.game.expReq   = er;
    uint32_t bc; EEPROM.get(ADDR_BTN_COUNT, bc); app.game.btnCount = bc;
    float  vol; EEPROM.get(ADDR_VOLUME,     vol);
    if (vol >= 0.0f && vol <= 1.0f) app.volume = vol;

    int16_t d, m, y;
    EEPROM.get(ADDR_LAST_DAY,   d);
    EEPROM.get(ADDR_LAST_MONTH, m);
    EEPROM.get(ADDR_LAST_YEAR,  y);
    app.savedDate = {d, m, y};

    if (ver >= 6) {
        for (int i = 0; i < 3; i++) {
            uint32_t t; EEPROM.get(ADDR_TIMER_LOGS + i * 4, t);
            app.taskPrio.logs[i] = t;
        }
        int16_t tc; EEPROM.get(ADDR_TIMER_COUNT, tc);
        app.taskPrio.logCount = tc;

        EEPROM.get(ADDR_HEAT_DAY,   d);
        EEPROM.get(ADDR_HEAT_MONTH, m);
        EEPROM.get(ADDR_HEAT_YEAR,  y);
        app.heatmap.lastDate = {d, m, y};

        int16_t st; EEPROM.get(ADDR_STREAK, st); app.heatmap.streak = st;
        uint32_t bits; EEPROM.get(ADDR_GRID, bits);
        for (int i = 0; i < HEATMAP_CELLS; i++) app.heatmap.grid[i] = (bits >> i) & 1;

        uint8_t lt; EEPROM.get(ADDR_LOGGED_TODAY, lt);
        app.heatmap.loggedToday = (lt != 0);
    }

    if (ver >= 7) {
        uint8_t swRunning; EEPROM.get(ADDR_SW_RUNNING, swRunning);
        if (swRunning) {
            uint32_t swElapsed, swSaveUnix;
            EEPROM.get(ADDR_SW_ELAPSED,   swElapsed);
            EEPROM.get(ADDR_SW_SAVE_UNIX, swSaveUnix);
            // Correct for real time elapsed since last checkpoint via RTC
            uint32_t nowUnix = (uint32_t)rtc.now().unixtime();
            if (nowUnix >= swSaveUnix) swElapsed += (nowUnix - swSaveUnix) * 1000UL;
            app.taskPrio.running   = true;
            app.taskPrio.startTime = (uint32_t)(millis() - swElapsed);
        }
    }
    // Versions < 6: heatmap and timer data default to zero (AppState struct defaults).

    // Sanity-check core fields. If any are outside the range that normal
    // operation could produce, the save is corrupt (e.g. layout mismatch that
    // was written back before we could detect it). Reset everything to defaults.
    bool sane = (app.game.level >= 1 && app.game.level <= 9999)
             && (app.game.expReq > 0.0f   && app.game.expReq < 1e6f)
             && (app.game.exp   >= 0.0f   && app.game.exp   < 1e6f)
             && (app.volume     >= 0.0f   && app.volume     <= 1.0f);
    if (!sane) {
        app.game     = GameState{};
        app.heatmap  = HeatmapState{};
        app.taskPrio = TaskPrioState{};
        app.volume   = 0.2f;
        app.savedDate = SavedDate{};   // force the fresh-save path in setup()
        return;
    }

    // Compact timer logs — remove zero-time entries that can appear from corrupt saves
    int valid = 0;
    for (int i = 0; i < 3; i++) {
        if (app.taskPrio.logs[i] > 0) {
            app.taskPrio.logs[valid++] = app.taskPrio.logs[i];
        }
    }
    for (int i = valid; i < 3; i++) app.taskPrio.logs[i] = 0;
    app.taskPrio.logCount = valid;
}

void checkDayRollover() {
    DateTime now = rtc.now();
    SavedDate today = {(int16_t)now.day(), (int16_t)now.month(), (int16_t)now.year()};
    app.today = today;

    if (!app.savedDate.valid() || today == app.savedDate) return;

    // New day: reset daily counters
    app.game.btnCount          = 0;
    memset(app.taskPrio.logs,   0, sizeof(app.taskPrio.logs));
    app.taskPrio.logCount      = 0;
    app.taskPrio.firstUseToday = true;
    app.heatmap.loggedToday    = false;

    // Proper streak check using full-date calendar arithmetic.
    // Original code used (day - lastDay + 31) % 31 which broke at month boundaries
    // (e.g. day 30 → day 1 of next month incorrectly appeared as a 2-day gap).
    if (app.heatmap.lastDate.valid() && !app.heatmap.lastDate.isYesterdayOf(today)) {
        app.heatmap.streak = 0;
        memset(app.heatmap.grid, 0, sizeof(app.heatmap.grid));
    }

    app.savedDate = today;
    app.dirty     = true;
    saveData();
}
