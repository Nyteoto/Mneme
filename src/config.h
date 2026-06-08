#pragma once

// ── Display (software SPI) ────────────────────────────────────────────────
#define OLED_MOSI  19
#define OLED_CLK   18
#define OLED_DC    16
#define OLED_CS    17
#define OLED_RESET 20

// ── GPIO ──────────────────────────────────────────────────────────────────
#define PIN_BUTTON   0
#define BUTTON_LED   1
#define GREEN_LED   15
#define RED_LED     14
#define VBUS_PIN    24   // HIGH when USB bus is powered
#define BATT_ADC_PIN 28

// ── Audio I2S → MAX98357A (kept wired, no longer used for fanfare) ─────────
#define I2S_BCLK  7    // bit clock
#define I2S_DOUT  6    // data

// ── MCP23X17 ──────────────────────────────────────────────────────────────
#define MCP_ADDR 0x20

// ── Audio (amp kept wired; module compiles though fanfare is gone) ────────
#define AUDIO_SAMPLE_RATE 44100

// ── Thermal printer (UART) — module not yet wired; pin reserved ───────────
#define PRINTER_TX_PIN  8     // RP2040 → printer RX (placeholder until module lands)
#define PRINTER_BAUD    9600

// ── Channels / sections (the new data model) ──────────────────────────────
#define NUM_CHANNELS    9     // usable rotary positions (one switch pin is dead)
#define MAX_SECTIONS   16     // boundaries stored per channel
#define SECS_PER_DAY   86400UL

// On-screen timeline window: the horizontal bar spans this many days.
#define VIS_WINDOW_DAYS 90    // ~3 months visualized end-to-end

// ── EEPROM layout v8 — channels replace the gamified state ────────────────
// Channels are written as whole POD structs via EEPROM.put(), so the only
// fixed offsets we need are the header fields and the channel block base.
#define EEPROM_SIZE        2048
#define ADDR_MAGIC            0   // uint16_t (2)
#define ADDR_VERSION         2   // uint8_t  (1)
#define ADDR_VOLUME          4   // float    (4)
#define ADDR_PRINTCFG        8   // PrintConfig struct
#define ADDR_CHANNELS       32   // Channel[NUM_CHANNELS] packed from here

#define EEPROM_MAGIC    0x55AA
#define EEPROM_VERSION  8        // bumped: tracker rewrite (channels/sections)

// ── Battery thresholds — Li-ion healthy range ─────────────────────────────
#define BATT_FULL_V      4.05f
#define BATT_WARN_V      3.60f
#define BATT_CRITICAL_V  3.50f
#define BATT_FLASH_V     3.55f
#define BATT_SHUTDOWN_V  3.48f
#define BATT_EMPTY_V     3.45f
#define BATT_CUTOFF_V    3.30f
#define BATT_RECOVERY_V  3.60f
#define BATT_DIVIDER     2.0f
#define ADC_MAX          4095.0f

// ── Timing (ms) ───────────────────────────────────────────────────────────
#define SLEEP_TIMEOUT_MS     600000UL   // 10 min active → sleep
#define SHUTDOWN_TIMEOUT_MS  300000UL   // 5 min sleep → shutdown
#define SAVE_INTERVAL_MS     300000UL   // flush dirty EEPROM every 5 min
#define POWER_CHECK_MS         5000UL   // USB/battery source check interval
#define HOLD_MS                 700UL   // long-hold threshold → open menu
#define CONFIRM_TIMEOUT_MS     6000UL   // "section off?" auto-cancels after this
#define PRINTING_MS            2500UL   // "Printing..." splash duration (stub)

// ── Enums ─────────────────────────────────────────────────────────────────
enum PowerSource { POWER_BATTERY, POWER_USB };
enum DeviceState { STATE_ACTIVE, STATE_SLEEP, STATE_SHUTDOWN };

// UI modes — the single button + rotary drive all of these.
enum UiMode {
    UI_MAIN,      // nav bar + bar chart for the selected channel
    UI_CONFIRM,   // "section off?" — second press commits, rotate cancels
    UI_MENU,      // long-hold action menu (Print / Settings / Cancel)
    UI_SETTINGS,  // print-config editor with live preview
    UI_PRINTING   // transient "Printing..." splash
};

enum MenuItem { MENU_PRINT, MENU_SETTINGS, MENU_CANCEL, MENU_COUNT };

// Print-settings editable fields (last entry = exit row).
enum SettingField { SET_FONT, SET_BOLD, SET_BARW, SET_DONE, SET_COUNT };

// ── Rotary switch: logical channel index → physical MCP pin ────────────────
// PROVISIONAL 9-channel map: the legacy 10-entry table was
//   {7, 6, 5, 10, 4, 3, 2, 1, 0, 9}
// Channel 3 sat on pin 5 (GPA5), which is dead on this unit, so it's dropped.
// Run the serial `SCAN` command and rotate through every detent to confirm
// the true pin-per-position wiring, then finalise this list.
static const int ROTARY_ORDER[NUM_CHANNELS] = {7, 6, 10, 4, 3, 2, 1, 0, 9};
#define ROTARY_COUNT NUM_CHANNELS

// MCP pins to configure/scan (full 16 so a stray-pin detent is detectable).
#define MCP_PIN_COUNT 16
