#include "input.h"
#include "config.h"
#include "state.h"
#include "game.h"
#include "audio.h"
#include "storage.h"
#include "power.h"
#include "peripherals.h"
#include <Arduino.h>

void inputInit() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(BUTTON_LED, OUTPUT);
}

// ── Rotary switch ─────────────────────────────────────────────────────────

static int readRotaryPos() {
    for (int i = 0; i < 11; i++) {
        if (mcp.digitalRead(i) == LOW) return i;
    }
    return -1;
}

static int rotaryPosToIndex(int pos) {
    for (int i = 0; i < ROTARY_COUNT; i++) {
        if (ROTARY_ORDER[i] == pos) return i;
    }
    return -1;
}

void handleRotary() {
    int pos = readRotaryPos();
    if (pos == -1 || pos == app.lastRotaryPos) return;

    app.pwr.lastActivity = millis();
    int idx = rotaryPosToIndex(pos);
    if (idx != -1 && app.lastRotaryIdx != -1) {
        int dir = idx - app.lastRotaryIdx;
        int next = ((int)app.screen + (dir > 0 ? 1 : -1) + SCREEN_COUNT) % SCREEN_COUNT;
        app.screen = (ScreenMode)next;
    }
    app.lastRotaryPos = pos;
    app.lastRotaryIdx = idx;
}

// ── Button ────────────────────────────────────────────────────────────────

static void handleShortPress() {
    switch (app.screen) {
        case SCREEN_HOME:
            if (app.eval.mode == EVAL_INACTIVE) {
                app.game.btnCount++;
                addExp(1.0f);
                app.dirty = true;
            }
            break;

        case SCREEN_TASK_INIT:
            if (!app.taskInit.running && !app.taskInit.finished) {
                app.taskInit.startTime = millis();
                app.taskInit.running   = true;
            } else if (app.taskInit.finished) {
                app.taskInit.running  = false;
                app.taskInit.finished = false;
            }
            break;

        case SCREEN_TASK_PRIO:
            if (!app.taskPrio.running) {
                app.taskPrio.startTime = millis();
                app.taskPrio.running   = true;
            } else {
                uint32_t elapsed = millis() - app.taskPrio.startTime;
                app.taskPrio.running = false;
                if (app.taskPrio.logCount < 3) {
                    app.taskPrio.logs[app.taskPrio.logCount++] = elapsed;
                    app.taskPrio.firstUseToday = false;
                    app.dirty = true;
                    saveData();
                }
            }
            break;

        case SCREEN_HEATMAP:
            heatmapButtonPressed();
            break;

        default: break;
    }
}

static void handleHoldRelease() {
    if (app.screen != SCREEN_HOME) return;
    if (app.eval.mode != EVAL_HOLDING) return;

    app.eval.mode     = EVAL_DISPLAYING;
    app.eval.dispStart = millis();
    app.eval.msgIndex  = (uint8_t)random(EVAL_MSG_COUNT);

    app.game.btnCount++;
    addExp(10.0f);
    app.dirty = true;
    saveData();
    playSlowChime();
}

void handleButton() {
    bool cur = digitalRead(PIN_BUTTON);
    uint32_t now = millis();

    digitalWrite(BUTTON_LED, cur == LOW ? HIGH : LOW);

    // Press start
    if (cur == LOW && app.lastBtn == HIGH) {
        app.pwr.lastActivity = now;
        app.btnPressStart    = now;
        app.btnHeld          = false;
    }

    // Held past threshold — start task eval animation on home screen
    if (cur == LOW && app.lastBtn == LOW) {
        if (!app.btnHeld && !app.pwr.ignoreNextWakeRelease &&
                (now - app.btnPressStart) >= HOLD_MS) {
            app.btnHeld = true;
            if (app.screen == SCREEN_HOME && app.eval.mode == EVAL_INACTIVE) {
                app.eval.mode      = EVAL_HOLDING;
                app.eval.holdStart = now;
            }
        }
    }

    // Release
    if (cur == HIGH && app.lastBtn == LOW) {
        if (app.pwr.ignoreNextWakeRelease) {
            app.pwr.ignoreNextWakeRelease = false;
        } else if (app.btnHeld) {
            handleHoldRelease();
        } else {
            handleShortPress();
        }
        app.btnHeld = false;
    }

    // Task init timer complete check (fires from display update but also guard here)
    if (app.screen == SCREEN_TASK_INIT && app.taskInit.running) {
        if (now - app.taskInit.startTime >= TASK_INIT_MS) {
            app.taskInit.running  = false;
            app.taskInit.finished = true;
            playTaskCompleteDing();
        }
    }

    app.lastBtn = cur;
}

// ── Sleep-state polling ───────────────────────────────────────────────────
// Runs instead of handleButton/handleRotary in sleep and shutdown states.
// Kept very lightweight to minimise wake-up latency.

void handleSleepInput() {
    static uint32_t lastCheck = 0;
    uint32_t now = millis();
    if (now - lastCheck < 50) return;
    lastCheck = now;

    bool cur = digitalRead(PIN_BUTTON);
    if (cur == LOW && app.lastBtn == HIGH) {
        app.pwr.ignoreNextWakeRelease = true;
        wakeUp();
    }
    app.lastBtn = cur;

    static uint32_t lastRotaryCheck = 0;
    if (now - lastRotaryCheck >= 200) {
        int pos = readRotaryPos();
        if (pos != -1 && pos != app.lastRotaryPos) {
            wakeUp();
            app.lastRotaryPos = pos;
        }
        lastRotaryCheck = now;
    }
}
