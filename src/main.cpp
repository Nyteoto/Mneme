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
#include "audio.h"
#include "power.h"
#include "storage.h"
#include "game.h"
#include "screens.h"
#include "input.h"

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
        app.game      = GameState{};
        app.heatmap   = HeatmapState{};
        app.taskPrio  = TaskPrioState{};
        app.savedDate = app.today;
        saveData();
        Serial.println("DATA RESET");

    } else if (cmd.startsWith("VOL ")) {
        int pct = cmd.substring(4).toInt();
        if (pct >= 0 && pct <= 100) {
            app.volume = pct / 100.0f;
            app.dirty  = true;
            saveData();
            Serial.printf("VOLUME: %d%%\n", pct);
            playTone(440, 200, 0.1f);
            delay(50);
            audioShutdown();
        } else {
            Serial.println("ERROR: 0-100");
        }

    } else if (cmd == "VOL") {
        Serial.printf("VOLUME: %d%%\n", (int)(app.volume * 100));

    } else if (cmd == "TEST AUDIO") {
        playStartupChime();

    } else if (cmd == "STATUS") {
        Serial.printf("LEVEL:   %d\n",  app.game.level);
        Serial.printf("EXP:     %.1f / %.1f\n", app.game.exp, app.game.expReq);
        Serial.printf("BUTTONS: %lu\n", (unsigned long)app.game.btnCount);
        Serial.printf("STREAK:  %d\n",  app.heatmap.streak);
        Serial.printf("POWER:   %s\n",  app.pwr.source == POWER_USB ? "USB" : "BATTERY");
        Serial.printf("VOLTAGE: %.2fV\n", readBatteryVoltage());
        Serial.printf("BATT%%:   %d%%\n", batteryPercent(readBatteryVoltage()));
        const char* states[] = {"ACTIVE", "SLEEP", "SHUTDOWN"};
        Serial.printf("STATE:   %s\n",  states[app.pwr.device]);

    } else if (cmd == "BATT") {
        int raw = analogRead(BATT_ADC_PIN);
        float adcV = (float)raw / ADC_MAX * 3.3f;
        float battV = adcV * BATT_DIVIDER;
        Serial.printf("ADC raw: %d\n", raw);
        Serial.printf("ADC voltage (GPIO28): %.3fV\n", adcV);
        Serial.printf("Battery (x%.1f): %.3fV\n", BATT_DIVIDER, battV);
        Serial.println("Measure actual battery voltage with a multimeter,");
        Serial.println("then use: BATT_DIVIDER = actual_V / battV * current_BATT_DIVIDER");

    } else if (cmd == "HELP") {
        Serial.println("COMMANDS: RESET DATA | VOL [0-100] | TEST AUDIO | STATUS | BATT | HELP");

    } else if (cmd.length() > 0) {
        Serial.println("Unknown command. Type HELP.");
    }
}

// ── setup ─────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // Seed RNG from ADC noise
    uint32_t seed = 0;
    for (int i = 0; i < 16; i++) {
        seed = (seed << 2) ^ (uint32_t)analogRead(BATT_ADC_PIN) ^ micros();
        delay(1);
    }
    randomSeed(seed);

    storageInit();

    // Display must come before Wire.begin() — it is SPI, not I2C
    if (!display.begin(SSD1306_SWITCHCAPVCC)) { while (true); }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    showBootScreen();

    Wire.begin();

    // RTC: check if device found and running; set compile-time fallback if not
    if (!rtc.begin()) {
        // Could not find RTC on I2C — halt or continue without time
        // For now: continue; day-rollover simply won't fire
    } else if (!rtc.isrunning()) {
        // Clock found but not ticking (first use or dead backup battery)
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    if (!mcp.begin_I2C(MCP_ADDR)) { while (true); }
    for (int i = 0; i < 11; i++) mcp.pinMode(i, INPUT_PULLUP);

    powerInit();
    inputInit();
    loadData();

    // Sync today's date; checkDayRollover also sets app.today from RTC
    checkDayRollover();

    // If no valid save was found (fresh device or wiped EEPROM), write clean
    // defaults immediately so the next boot loads correct v6 data straight away.
    if (!app.savedDate.valid()) {
        app.savedDate = app.today;
        saveData();
    }

    app.taskPrio.firstUseToday = (app.taskPrio.logCount == 0);
    app.lastSaveTime = millis();

    // Read the current rotary position at boot so lastRotaryIdx starts valid.
    // Without this, the first physical movement sets lastRotaryIdx but can't
    // compute a direction (needs two known positions), so the first rotation
    // after boot is always silently dropped.
    for (int i = 0; i < 11; i++) {
        if (mcp.digitalRead(i) == LOW) {
            app.lastRotaryPos = i;
            for (int j = 0; j < ROTARY_COUNT; j++) {
                if (ROTARY_ORDER[j] == i) { app.lastRotaryIdx = j; break; }
            }
            break;
        }
    }

    // Attempt initial source check (may not fire if millis < POWER_CHECK_MS)
    powerTick();

    playStartupChime();

    Serial.println("MNEME READY — type HELP for commands");
}

// ── loop ──────────────────────────────────────────────────────────────────

void loop() {
    powerTick();

    // Allow USB to revive a shutdown state
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
            checkDayRollover();
            updateLevelUp();
            checkAutoSleep();

            if (app.screen == SCREEN_HOME) updateTaskEval();

            // Stopwatch checkpoint and limit enforcement
            if (app.taskPrio.running) {
                uint32_t swElapsed = millis() - app.taskPrio.startTime;
                if (swElapsed >= SW_LIMIT_MS) {
                    app.taskPrio.running   = false;
                    app.taskPrio.startTime = 0;
                    app.swLimitNotif      = true;
                    app.swLimitNotifStart  = millis();
                    saveData();
                } else {
                    static uint32_t lastSwCheckpoint = 0;
                    if (millis() - lastSwCheckpoint >= 60000UL) {
                        lastSwCheckpoint = millis();
                        saveData();
                    }
                }
            }

            // Dirty-flag EEPROM flush: only write every SAVE_INTERVAL_MS or on events
            if (app.dirty && millis() - app.lastSaveTime >= SAVE_INTERVAL_MS) {
                saveData();
            }

            updateDisplay();
            break;

        case STATE_SLEEP:
            handleSleepInput();
            checkAutoSleep();       // may transition to shutdown
            checkEmergencyShutdown();
            break;

        case STATE_SHUTDOWN:
            handleSleepInput();     // still allow button wake if USB restores power
            delay(500);
            break;
    }

    delay(10);
}
