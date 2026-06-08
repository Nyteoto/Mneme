#include "input.h"
#include "config.h"
#include "state.h"
#include "channels.h"
#include "storage.h"
#include "power.h"
#include "print.h"
#include "peripherals.h"
#include <Arduino.h>

void inputInit() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(BUTTON_LED, OUTPUT);
}

// ── Rotary switch (10-position absolute → channel / menu selector) ─────────

static int readRotaryPos() {
    for (int i = 0; i < MCP_PIN_COUNT; i++) {
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

    if (idx != -1) {
        int dir = (app.lastRotaryIdx != -1) ? (idx - app.lastRotaryIdx) : 0;
        switch (app.ui) {
            case UI_MAIN:
                app.currentChannel = (uint8_t)idx;     // absolute switch → channel
                break;
            case UI_CONFIRM:
            case UI_CLEAR_CONFIRM:
                app.ui = UI_MAIN;                       // turning away cancels
                app.currentChannel = (uint8_t)idx;
                break;
            case UI_MENU:
                if (dir) app.menuSel = (uint8_t)((app.menuSel + (dir > 0 ? 1 : -1) + MENU_COUNT) % MENU_COUNT);
                break;
            case UI_SETTINGS:
                if (dir) app.settingField = (uint8_t)((app.settingField + (dir > 0 ? 1 : -1) + SET_COUNT) % SET_COUNT);
                break;
            default: break;
        }
    }

    app.lastRotaryPos = pos;
    app.lastRotaryIdx = idx;
}

// ── Button actions ─────────────────────────────────────────────────────────

static void onLongHold() {
    // Long-hold from the main view opens the action menu.
    if (app.ui == UI_MAIN) {
        app.ui      = UI_MENU;
        app.menuSel = MENU_PRINT;
    }
}

static void onShortPress() {
    uint32_t now = millis();
    switch (app.ui) {
        case UI_MAIN:
            // arm the "are you sure" confirm before committing a boundary
            app.ui      = UI_CONFIRM;
            app.uiTimer = now;
            break;

        case UI_CONFIRM:
            channelMarkSection(app.currentChannel);
            app.sectionBlinkUntil = now + SECTION_BLINK_MS;   // pulse the button LED
            app.ui = UI_MAIN;
            break;

        case UI_MENU:
            switch (app.menuSel) {
                case MENU_PRINT:
                    printChannel(app.currentChannel);
                    app.ui      = UI_PRINTING;
                    app.uiTimer = now;
                    break;
                case MENU_SETTINGS:
                    app.ui           = UI_SETTINGS;
                    app.settingField = SET_FONT;
                    break;
                case MENU_CLEAR:
                    app.ui      = UI_CLEAR_CONFIRM;   // confirm before wiping
                    app.uiTimer = now;
                    break;
                default:   // MENU_CANCEL
                    app.ui = UI_MAIN;
                    break;
            }
            break;

        case UI_CLEAR_CONFIRM:
            channelClear(app.currentChannel);
            app.ui = UI_MAIN;
            break;

        case UI_SETTINGS:
            switch (app.settingField) {
                case SET_FONT: app.print.fontSize = app.print.fontSize >= 3 ? 1 : app.print.fontSize + 1; app.dirty = true; break;
                case SET_BOLD: app.print.bold ^= 1; app.dirty = true; break;
                case SET_BARW: app.print.barWidth = app.print.barWidth >= 10 ? 2 : app.print.barWidth + 2; app.dirty = true; break;
                default:       saveData(); app.ui = UI_MENU; break;   // SET_DONE
            }
            break;

        default: break;   // UI_PRINTING auto-dismisses
    }
}

void handleButton() {
    bool cur = digitalRead(PIN_BUTTON);
    uint32_t now = millis();

    // Pulse the button LED for a moment after a section is cut; otherwise it
    // simply mirrors the press.
    if (now < app.sectionBlinkUntil)
        digitalWrite(BUTTON_LED, (now / 150) % 2 ? HIGH : LOW);
    else
        digitalWrite(BUTTON_LED, cur == LOW ? HIGH : LOW);

    // press start
    if (cur == LOW && app.lastBtn == HIGH) {
        app.pwr.lastActivity = now;
        app.btnPressStart    = now;
        app.btnHeld          = false;
    }

    // crossed the hold threshold while still down
    if (cur == LOW && app.lastBtn == LOW) {
        if (!app.btnHeld && !app.pwr.ignoreNextWakeRelease &&
                (now - app.btnPressStart) >= HOLD_MS) {
            app.btnHeld = true;
            onLongHold();
        }
    }

    // release
    if (cur == HIGH && app.lastBtn == LOW) {
        if (app.pwr.ignoreNextWakeRelease) {
            app.pwr.ignoreNextWakeRelease = false;
        } else if (!app.btnHeld) {
            onShortPress();
        }
        app.btnHeld = false;
    }

    app.lastBtn = cur;
}

// ── UI timeouts ─────────────────────────────────────────────────────────────

void uiTick() {
    uint32_t now = millis();
    if ((app.ui == UI_CONFIRM || app.ui == UI_CLEAR_CONFIRM) &&
            now - app.uiTimer >= CONFIRM_TIMEOUT_MS) {
        app.ui = UI_MAIN;
    } else if (app.ui == UI_PRINTING && now - app.uiTimer >= PRINTING_MS) {
        app.ui = UI_MAIN;
    }
}

// ── Sleep-state polling ─────────────────────────────────────────────────────

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
