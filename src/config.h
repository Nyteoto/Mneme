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

// ── Audio I2S → MAX98357A ─────────────────────────────────────────────────
#define I2S_BCLK  7    // bit clock
#define I2S_DOUT  6    // data
// LRCLK = I2S_BCLK + 1 = pin 8 (assigned automatically by earlephilhower I2S lib)

// ── MCP23X17 ──────────────────────────────────────────────────────────────
#define MCP_ADDR 0x20

// ── EEPROM layout v7 ──────────────────────────────────────────────────────
// Each slot's type determines the byte count consumed by EEPROM.put().
// Total used: ~70 bytes of 512 available.
#define EEPROM_SIZE          512
#define ADDR_MAGIC             0   // uint16_t (2)
#define ADDR_VERSION           2   // uint8_t  (1)
#define ADDR_LEVEL             4   // int32_t  (4)
#define ADDR_EXP               8   // float    (4)
#define ADDR_EXP_REQ          12   // float    (4)
#define ADDR_BTN_COUNT        16   // uint32_t (4)
#define ADDR_VOLUME           20   // float    (4)
#define ADDR_LAST_DAY         24   // int16_t  (2)
#define ADDR_LAST_MONTH       26   // int16_t  (2)
#define ADDR_LAST_YEAR        28   // int16_t  (2)
#define ADDR_TIMER_LOGS       32   // 3x uint32_t (12)
#define ADDR_TIMER_COUNT      44   // int16_t  (2)
#define ADDR_HEAT_DAY         48   // int16_t  (2)
#define ADDR_HEAT_MONTH       50   // int16_t  (2)
#define ADDR_HEAT_YEAR        52   // int16_t  (2)
#define ADDR_STREAK           54   // int16_t  (2)
#define ADDR_GRID             56   // uint32_t (4)
#define ADDR_LOGGED_TODAY     60   // uint8_t  (1)
#define ADDR_SW_RUNNING       61   // uint8_t  (1) — was stopwatch running at last save?
#define ADDR_SW_ELAPSED       62   // uint32_t (4) — elapsed ms at last checkpoint
#define ADDR_SW_SAVE_UNIX     66   // uint32_t (4) — RTC unix time of last checkpoint

#define EEPROM_MAGIC    0x55AA
#define EEPROM_VERSION  7      // bumped: stopwatch persistence across power loss

// ── Audio ─────────────────────────────────────────────────────────────────
#define AUDIO_SAMPLE_RATE 44100

// ── Battery thresholds — Li-ion healthy range ─────────────────────────────
#define BATT_FULL_V      4.05f   // healthy full (not 4.2 to reduce stress)
#define BATT_WARN_V      3.60f   // start warning LEDs
#define BATT_CRITICAL_V  3.50f   // red LED
#define BATT_FLASH_V     3.55f   // flash battery indicator
#define BATT_SHUTDOWN_V  3.48f   // graceful shutdown
#define BATT_EMPTY_V     3.45f   // emergency shutdown
#define BATT_CUTOFF_V    3.30f   // hard cutoff, no recovery except USB
#define BATT_RECOVERY_V  3.60f   // re-activation threshold
#define BATT_DIVIDER     2.0f
#define ADC_MAX          4095.0f

// ── Timing (ms unless noted) ──────────────────────────────────────────────
#define SLEEP_TIMEOUT_MS     600000UL   // 10 min active → sleep
#define SHUTDOWN_TIMEOUT_MS  300000UL   // 5 min sleep → shutdown
#define SAVE_INTERVAL_MS     300000UL   // flush dirty EEPROM every 5 min
#define POWER_CHECK_MS         5000UL   // USB/battery source check interval
#define HOLD_MS                 700UL   // button hold threshold
#define EVAL_DISPLAY_MS       10000UL   // task-eval message display duration
#define EVAL_ANIM_MS           5000UL   // expanding-circle animation duration
#define TASK_INIT_MS         120000UL   // 2-minute focus countdown
#define LEVEL_UP_MS            8500UL   // level-up animation duration
#define HEATMAP_FLASH_MS       2000UL

// ── Heatmap ───────────────────────────────────────────────────────────────
#define HEATMAP_CELLS         21
#define HEATMAP_COLS           7
#define HEATMAP_ROWS           3
#define HEATMAP_FLASH_CYCLES   8
#define HEATMAP_COMPLETE_EXP  50.0f

// ── Stopwatch limit ───────────────────────────────────────────────────────
#define SW_LIMIT_MS     3600000000UL  // 1000 hours in ms (display buffer limit)
#define SW_NOTIF_MS          10000UL  // notification display duration

// ── Workday reference for task-priority percentage ─────────────────────────
#define WORKDAY_MS  57600000UL   // 16 hours

// ── Task evaluation messages ──────────────────────────────────────────────
#define EVAL_MSG_COUNT 11

// ── Rotary switch: physical pin → logical screen order ────────────────────
static const int ROTARY_ORDER[10] = {7, 6, 5, 10, 4, 3, 2, 1, 0, 9};
#define ROTARY_COUNT 10

// ── Enums ─────────────────────────────────────────────────────────────────
enum PowerSource   { POWER_BATTERY, POWER_USB };
enum DeviceState   { STATE_ACTIVE, STATE_SLEEP, STATE_SHUTDOWN };
enum ScreenMode    { SCREEN_HOME, SCREEN_TASK_INIT, SCREEN_TASK_PRIO, SCREEN_HEATMAP, SCREEN_COUNT };
enum TaskEvalState { EVAL_INACTIVE, EVAL_HOLDING, EVAL_DISPLAYING };
