#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MCP23X17.h>
#include "RTClib.h"
#include <EEPROM.h>

#include "config.h"
#include "peripherals.h"
#include "state.h"
#include "power.h"
#include "storage.h"
#include "channels.h"
#include "screens.h"
#include "input.h"
#include "print.h"

// ── Peripheral objects (extern-declared in peripherals.h) ──────────────────
Adafruit_SSD1306  display(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
Adafruit_MCP23X17 mcp;
RTC_DS1307        rtc;

// ── Serial debug commands ──────────────────────────────────────────────────
static void handleSerial() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "RESET DATA") {
        channelsClear();
        saveData();
        Serial.println("CHANNELS CLEARED");

    } else if (cmd == "DEMO") {
        channelsInitDemo();
        saveData();
        Serial.println("DEMO DATA LOADED");

    } else if (cmd.startsWith("PRINT ")) {
        int n = cmd.substring(6).toInt();
        if (n >= 1 && n <= NUM_CHANNELS) printChannel((uint8_t)(n - 1));
        else Serial.println("ERROR: PRINT 1-8");

    } else if (cmd == "STATUS") {
        for (int i = 0; i < NUM_CHANNELS; i++) {
            const Channel& c = app.channels[i];
            if (!channelUsed(c)) continue;
            Serial.printf("CH%-2d  %lud  S%lu\n", i + 1,
                          (unsigned long)channelTotalDays(c),
                          (unsigned long)c.sectionCount);
        }
        Serial.printf("POWER:   %s\n", app.pwr.source == POWER_USB ? "USB" : "BATTERY");
        Serial.printf("VOLTAGE: %.2fV\n", readBatteryVoltage());
        const char* states[] = {"ACTIVE", "SLEEP", "SHUTDOWN"};
        Serial.printf("STATE:   %s\n", states[app.pwr.device]);

    } else if (cmd == "BATT") {
        int raw = analogRead(BATT_ADC_PIN);
        float adcV = (float)raw / ADC_MAX * 3.3f;
        Serial.printf("ADC raw: %d  GPIO28: %.3fV  Batt: %.3fV\n",
                      raw, adcV, adcV * BATT_DIVIDER);

    } else if (cmd == "SCAN") {
        // Rotary diagnostic: rotate slowly through every detent; this prints
        // the MCP pin(s) reading LOW each time the active set changes, giving
        // the true physical-position → pin map (and exposing dead pins).
        Serial.println("ROTARY SCAN (25s) — turn through every detent slowly.");
        for (int i = 0; i < MCP_PIN_COUNT; i++) mcp.pinMode(i, INPUT_PULLUP);
        int lastMask = -1;
        uint32_t t0 = millis();
        while (millis() - t0 < 25000UL) {
            int mask = 0;
            for (int i = 0; i < MCP_PIN_COUNT; i++)
                if (mcp.digitalRead(i) == LOW) mask |= (1 << i);
            if (mask != lastMask) {
                lastMask = mask;
                Serial.print("  LOW: ");
                if (mask == 0) Serial.print("(none)");
                else for (int i = 0; i < MCP_PIN_COUNT; i++)
                    if (mask & (1 << i)) { Serial.print(i); Serial.print(' '); }
                Serial.println();
            }
            delay(40);
        }
        Serial.println("SCAN done.");

    } else if (cmd == "HELP") {
        Serial.println("COMMANDS: RESET DATA | DEMO | PRINT [1-8] | STATUS | BATT | SCAN | HELP");

    } else if (cmd.length() > 0) {
        Serial.println("Unknown command. Type HELP.");
    }
}

// ── setup ─────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    storageInit();

    // Display is SPI; it must init before Wire.begin().
    if (!display.begin(SSD1306_SWITCHCAPVCC)) { while (true); }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    showBootScreen();

    Wire.begin();

    if (!rtc.begin()) {
        // No RTC found — timestamps will read 0; day-counts stay at 0 but the
        // device still runs. (RTC is required for real history.)
    } else if (!rtc.isrunning()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    if (!mcp.begin_I2C(MCP_ADDR)) { while (true); }
    for (int i = 0; i < MCP_PIN_COUNT; i++) mcp.pinMode(i, INPUT_PULLUP);

    powerInit();
    inputInit();

    // Fresh device (or wiped EEPROM): seed demo data so the UI has something
    // to render while we design it, then persist it.
    if (!loadData()) {
        channelsInitDemo();
        saveData();
    }

    app.lastSaveTime = millis();

    // Seed the rotary position so the first turn has a known origin and the
    // current channel matches the physical switch at boot.
    for (int i = 0; i < MCP_PIN_COUNT; i++) {
        if (mcp.digitalRead(i) == LOW) {
            app.lastRotaryPos = i;
            int idx = -1;
            for (int j = 0; j < ROTARY_COUNT; j++) {
                if (ROTARY_ORDER[j] == i) { idx = j; break; }
            }
            app.lastRotaryIdx = idx;
            if (idx != -1) app.currentChannel = (uint8_t)idx;
            break;
        }
    }

    powerTick();
    Serial.println("MNEME READY — type HELP for commands");
}

// ── loop ──────────────────────────────────────────────────────────────────

void loop() {
    powerTick();

    if (app.pwr.device == STATE_SHUTDOWN && app.pwr.source == POWER_USB) {
        wakeUp();
    }

    checkEmergencyShutdown();
    handleSerial();

    switch (app.pwr.device) {
        case STATE_ACTIVE:
            updateBatteryLEDs();
            handleButton();
            handleRotary();
            uiTick();

            // Blink the red status LED (GP14) while a print is in progress.
            // Runs after updateBatteryLEDs so it overrides the battery state.
            if (app.ui == UI_PRINTING)
                digitalWrite(RED_LED, (millis() / 150) % 2 ? HIGH : LOW);

            checkAutoSleep();

            // Dirty-flag EEPROM flush: write at most every SAVE_INTERVAL_MS.
            if (app.dirty && millis() - app.lastSaveTime >= SAVE_INTERVAL_MS) {
                saveData();
            }

            updateDisplay();
            break;

        case STATE_SLEEP:
            handleSleepInput();
            checkAutoSleep();
            checkEmergencyShutdown();
            break;

        case STATE_SHUTDOWN:
            handleSleepInput();
            delay(500);
            break;
    }

    delay(10);
}
