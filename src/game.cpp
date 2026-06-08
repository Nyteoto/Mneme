#include "game.h"
#include "config.h"
#include "state.h"
#include "audio.h"
#include "storage.h"
#include <math.h>
#include <Arduino.h>

// ── Level / EXP ───────────────────────────────────────────────────────────

float calcExpForLevel(int level) {
    float raw = 10.0f * logf((float)(level + 5));
    return roundf(raw * 100.0f) / 100.0f;
}

void addExp(float amount) {
    app.game.exp += amount;
    // Guard: if expReq is zero, NaN, or negative (e.g. from a corrupt load),
    // recalculate it from the current level rather than looping forever.
    if (!(app.game.expReq > 0.0f)) {
        app.game.expReq = calcExpForLevel(app.game.level);
    }
    bool leveled = false;
    while (app.game.exp >= app.game.expReq && app.game.expReq > 0.0f) {
        app.game.exp -= app.game.expReq;
        app.game.level++;
        app.game.expReq = calcExpForLevel(app.game.level);
        leveled = true;
        startLevelUp();
    }
    if (leveled) saveData();
}

void startLevelUp() {
    app.levelUpActive = true;
    app.levelUpStart  = millis();
    playLevelUpChime();
}

void updateLevelUp() {
    if (!app.levelUpActive) return;
    if (millis() - app.levelUpStart >= LEVEL_UP_MS) app.levelUpActive = false;
}

// ── Heatmap ───────────────────────────────────────────────────────────────

void resetHeatmap() {
    app.heatmap.streak      = 0;
    app.heatmap.loggedToday = false;
    memset(app.heatmap.grid, 0, sizeof(app.heatmap.grid));
    app.dirty = true;
    saveData();
}

void heatmapButtonPressed() {
    if (app.heatmap.loggedToday) return;

    // Guard against a missed streak detected mid-session
    if (app.heatmap.lastDate.valid() && !app.heatmap.lastDate.isYesterdayOf(app.today)
        && app.heatmap.lastDate != app.today) {
        resetHeatmap();
        return;
    }

    app.heatmap.streak++;
    app.heatmap.lastDate    = app.today;
    app.heatmap.loggedToday = true;

    if (app.heatmap.streak <= HEATMAP_CELLS) {
        app.heatmap.grid[app.heatmap.streak - 1] = true;
    }

    if (app.heatmap.streak == HEATMAP_CELLS) {
        // 21-day completion: reward EXP, trigger flash, reset grid
        app.heatmap.flashActive = true;
        app.heatmap.flashStart  = millis();
        addExp(HEATMAP_COMPLETE_EXP);
        resetHeatmap();
    }

    app.dirty = true;
    saveData();
}
