#include "power.h"
#include "config.h"
#include "state.h"
#include "audio.h"
#include "storage.h"
#include "peripherals.h"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

extern "C" {
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/xosc.h"
#include "hardware/gpio.h"
}
#include "hardware/structs/rosc.h"
#include "hardware/regs/rosc.h"
#include "hardware/regs/clocks.h"


// ── Battery ───────────────────────────────────────────────────────────────

void powerInit() {
    pinMode(VBUS_PIN,     INPUT);
    pinMode(BATT_ADC_PIN, INPUT);
    pinMode(GREEN_LED,    OUTPUT);
    pinMode(RED_LED,      OUTPUT);
    pinMode(BUTTON_LED,   OUTPUT);
    analogReadResolution(12);
    app.pwr.lastActivity = millis();

    // Detect source immediately so the display is correct before the first powerTick
    app.pwr.source = digitalRead(VBUS_PIN) ? POWER_USB : POWER_BATTERY;

    // Seed EMA filter with a real read so the battery % is correct at boot
    if (app.pwr.source == POWER_BATTERY) {
        float v = readBatteryVoltage();
        if (v >= 2.5f) {
            app.pwr.filteredBattV = v;
            app.pwr.stableV       = v;
            app.pwr.stablePct     = batteryPercent(v);
        }
    } else {
        app.pwr.stableV   = BATT_FULL_V;
        app.pwr.stablePct = 100;
    }
}

float readBatteryVoltage() {
    const int N = 8;
    long sum = 0;
    for (int i = 0; i < N; i++) {
        delayMicroseconds(100);
        sum += analogRead(BATT_ADC_PIN);
    }
    float adcV = (float)(sum / N) / ADC_MAX * 3.3f;
    return adcV * BATT_DIVIDER;
}

int batteryPercent(float v) {
    v = constrain(v, BATT_EMPTY_V, BATT_FULL_V);
    return (int)((v - BATT_EMPTY_V) / (BATT_FULL_V - BATT_EMPTY_V) * 100.0f);
}

// ── USB / power source ────────────────────────────────────────────────────

void powerTick() {
    uint32_t now = millis();
    if (now - app.pwr.lastSrcCheck < POWER_CHECK_MS) return;
    app.pwr.lastSrcCheck = now;

    bool usb = digitalRead(VBUS_PIN);
    PowerSource newSrc = usb ? POWER_USB : POWER_BATTERY;
    if (newSrc != app.pwr.source) {
        app.pwr.source = newSrc;
        if (usb) {
            app.pwr.stableV   = BATT_FULL_V;
            app.pwr.stablePct = 100;
        }
        app.dirty = true;
    }

    if (!usb) {
        float raw = readBatteryVoltage();
        if (raw >= 2.5f) {
            // EMA: each new sample carries 30% weight — smooths ADC noise while
            // still tracking real voltage changes within a few read cycles
            app.pwr.filteredBattV = (app.pwr.filteredBattV > 0.0f)
                ? app.pwr.filteredBattV * 0.7f + raw * 0.3f
                : raw;
            app.pwr.stableV   = app.pwr.filteredBattV;
            app.pwr.stablePct = batteryPercent(app.pwr.filteredBattV);
        }
    }
}

// ── LED status ────────────────────────────────────────────────────────────

void updateBatteryLEDs() {
    if (app.pwr.source == POWER_USB) {
        digitalWrite(GREEN_LED, HIGH);
        digitalWrite(RED_LED,   LOW);
        return;
    }
    float v = app.pwr.filteredBattV;
    if (v < 2.5f) return;   // not yet seeded

    if (v <= BATT_CRITICAL_V) {
        digitalWrite(GREEN_LED, LOW);
        digitalWrite(RED_LED,   HIGH);
    } else if (v <= BATT_WARN_V) {
        bool blink = (millis() % 1000) < 500;
        digitalWrite(GREEN_LED, blink ? HIGH : LOW);
        digitalWrite(RED_LED,   LOW);
    } else {
        digitalWrite(GREEN_LED, HIGH);
        digitalWrite(RED_LED,   LOW);
    }

    if (v <= BATT_FLASH_V && !app.pwr.flashActive) {
        app.pwr.flashActive = true;
        app.pwr.flashStart  = millis();
    }
    if (app.pwr.flashActive && millis() - app.pwr.flashStart >= 3000) {
        app.pwr.flashActive = false;
    }
}

// ── Soft sleep / shutdown (CPU still running, peripherals off) ────────────

void enterSleep() {
    saveData();
    audioShutdown();
    pinMode(I2S_BCLK, INPUT);
    pinMode(I2S_DOUT, INPUT);
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    digitalWrite(BUTTON_LED, LOW);
    digitalWrite(GREEN_LED,  LOW);
    digitalWrite(RED_LED,    LOW);
    pinMode(BATT_ADC_PIN, INPUT);
    for (int i = 0; i < 11; i++) mcp.pinMode(i, INPUT);
    app.pwr.device    = STATE_SLEEP;
    app.pwr.sleepStart = millis();
}

void enterShutdown() {
    // No blocking loop — just set state. The main loop handles STATE_SHUTDOWN.
    audioShutdown();
    pinMode(I2S_BCLK, INPUT);
    pinMode(I2S_DOUT, INPUT);
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    digitalWrite(BUTTON_LED, LOW);
    digitalWrite(GREEN_LED,  LOW);
    digitalWrite(RED_LED,    LOW);
    pinMode(BATT_ADC_PIN, INPUT);
    app.pwr.device = STATE_SHUTDOWN;
}

void wakeUp() {
    app.pwr.device      = STATE_ACTIVE;
    app.pwr.lastActivity = millis();
    // Bug fix: original code forgot to restore MCP pins after sleep,
    // making the rotary switch non-functional until next boot.
    for (int i = 0; i < 11; i++) mcp.pinMode(i, INPUT_PULLUP);
    display.ssd1306_command(SSD1306_DISPLAYON);
    digitalWrite(GREEN_LED, HIGH);
    delay(50);
    digitalWrite(GREEN_LED, LOW);
    updateBatteryLEDs();
}

// ── Hardware dormant ──────────────────────────────────────────────────────
// True RP2040 dormant: switch system to ROSC, kill PLLs + XOSC, write the
// magic dormant value to the ROSC register. CPU halts until the button GPIO
// fires a falling-edge interrupt, then execution resumes from the line after
// the register write. Clocks are fully restored before returning.
// Power draw: ~0.1–0.5 mA vs ~1–2 mA for soft sleep.

static void clocksToROSC() {
    clock_configure(clk_ref,  CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH, 0,                                               6000000,  6000000);
    clock_configure(clk_sys,  CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,        0,                                               6000000,  6000000);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,                                                  6000000,  6000000);
    clock_configure(clk_usb,  0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH,                                            6000000,  6000000);
    clock_configure(clk_adc,  0, CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH,                                            6000000,  6000000);
    clock_configure(clk_rtc,  0, CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH,                                            6000000,    46875);
    pll_deinit(pll_sys);
    pll_deinit(pll_usb);
    xosc_disable();
}

static void clocksRestore() {
    xosc_init();
    pll_init(pll_sys, 1, 1500000000u, 6, 2);   // 125 MHz
    pll_init(pll_usb, 1,  480000000u, 5, 2);   //  48 MHz
    clock_configure(clk_ref,  CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,                              0,                    12000000,  12000000);
    clock_configure(clk_sys,  CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                              CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,                                             125000000, 125000000);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,                                               125000000, 125000000);
    clock_configure(clk_usb,  0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,                                          48000000,  48000000);
    clock_configure(clk_adc,  0, CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,                                          48000000,  48000000);
    clock_configure(clk_rtc,  0, CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,                                          48000000,    46875);
}

void enterDormant() {
    enterSleep();

    // Section timestamps come straight from the RTC, so nothing in the new
    // model needs millis()-correction across sleep — just dormant and resume.
    gpio_set_dormant_irq_enabled(PIN_BUTTON, GPIO_IRQ_EDGE_FALL, true);
    clocksToROSC();

    rosc_hw->dormant = ROSC_DORMANT_VALUE_DORMANT;
    // ── CPU resumes here on button press ──

    clocksRestore();
    gpio_acknowledge_irq(PIN_BUTTON, GPIO_IRQ_EDGE_FALL);
    gpio_set_dormant_irq_enabled(PIN_BUTTON, GPIO_IRQ_EDGE_FALL, false);

    app.pwr.ignoreNextWakeRelease = true;
    wakeUp();
}

// ── Automatic sleep / shutdown checks ────────────────────────────────────

void checkAutoSleep() {
    uint32_t now = millis();
    if (app.pwr.device == STATE_ACTIVE) {
        if (now - app.pwr.lastActivity >= SLEEP_TIMEOUT_MS) {
            enterDormant(); // use real hardware sleep instead of soft sleep
        }
    } else if (app.pwr.device == STATE_SLEEP) {
        // Soft sleep fallback: after SHUTDOWN_TIMEOUT enter shutdown state
        if (now - app.pwr.sleepStart >= SHUTDOWN_TIMEOUT_MS) enterShutdown();
    }
}

void checkEmergencyShutdown() {
    if (app.pwr.source == POWER_USB) return;
    float v = readBatteryVoltage();

    // Sanity floor: a healthy Li-ion is never below 2.5 V.
    // A reading that low means GPIO 28 is floating (disconnected wire).
    // Print a warning and skip — don't nuke the display over a bad ADC read.
    if (v < 2.5f) {
        static uint32_t lastWarn = 0;
        if (millis() - lastWarn >= 5000) {
            lastWarn = millis();
            Serial.printf("[power] ADC reads %.2fV — GPIO28 may be floating\n", v);
        }
        return;
    }

    if (v <= BATT_CUTOFF_V) {
        // Hard cutoff: save and halt. Recovery only via USB or power cycle.
        // No while(true) — we set STATE_SHUTDOWN and the main loop goes quiet.
        saveData();
        audioShutdown();
        Wire.end();
        SPI.end();
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        digitalWrite(BUTTON_LED, LOW);
        digitalWrite(GREEN_LED,  LOW);
        digitalWrite(RED_LED,    LOW);
        app.pwr.device = STATE_SHUTDOWN;
        return;
    }
    if (v <= BATT_EMPTY_V || v <= BATT_SHUTDOWN_V) {
        saveData();
        enterShutdown();
    }
}
