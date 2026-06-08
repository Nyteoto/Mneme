#include "screens.h"
#include "config.h"
#include "state.h"
#include "game.h"
#include "power.h"
#include "peripherals.h"
#include <Arduino.h>
#include <math.h>

// EVAL_MSG_COUNT is defined in config.h
static const char* EVAL_MESSAGES[] = {
    "Breathe.",
    "How do you feel about that?",
    "We still moving, then.",
    "Did it scare you?",
    "On a scale of 1-10, how life-changing?",
    "Another step logged.",
    "Maybe a friend would love to hear it.",
    "Attention returns to yourself now.",
    "Did it serve you well?",
    "It aligned with you, then.",
    "That is how a King exerts his sovereignty."
};

// ── Shared helpers ────────────────────────────────────────────────────────

static void drawBatteryBar() {
    const int x = 0, y = 56, w = 20, h = 8;
    bool draw = true;
    if (app.pwr.flashActive) draw = (millis() / 200) % 2 == 0;
    if (!draw) return;

    display.drawRect(x, y, w, h, SSD1306_WHITE);
    display.fillRect(x + w, y + 2, 2, h - 4, SSD1306_WHITE); // terminal nub

    int fill = (w - 2) * app.pwr.stablePct / 100;
    if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(x + w + 6, y);
    if (app.pwr.source == POWER_USB) {
        display.print("USB");
    } else {
        char buf[6];
        snprintf(buf, sizeof(buf), "%d%%", app.pwr.stablePct);
        display.print(buf);
    }
}

static void drawExpBar() {
    const int x = 88, y = 56, w = 40, h = 8;
    // Guard division by zero / NaN from a corrupt expReq
    int pct = (app.game.expReq > 0.0f)
        ? (int)(app.game.exp / app.game.expReq * 100.0f)
        : 0;
    pct = constrain(pct, 0, 100);
    display.drawRect(x, y, w, h, SSD1306_WHITE);
    int fill = (w - 2) * pct / 100;
    if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
}

static void drawButtonCounter() {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02lu", (unsigned long)app.game.btnCount);
    display.setTextSize(1);
    display.setCursor(73, 56);
    display.print(buf);
}

static void drawLevelUp() {
    if (!app.levelUpActive) return;
    uint32_t elapsed = millis() - app.levelUpStart;
    float intensity;
    if      (elapsed < 500)  intensity = (elapsed / 100) % 2 == 0 ? 1.0f : 0.0f;
    else if (elapsed < 1500) intensity = (elapsed / 200) % 2 == 0 ? 1.0f : 0.0f;
    else if (elapsed < 4500) intensity = 1.0f;
    else                     intensity = 1.0f - (elapsed - 4500.0f) / 4000.0f;
    if (intensity <= 0.1f) return;

    char lvBuf[4];
    snprintf(lvBuf, sizeof(lvBuf), "%02d", app.game.level);
    int16_t x1, y1; uint16_t tw, th;
    display.setTextSize(4);
    display.getTextBounds(lvBuf, 0, 0, &x1, &y1, &tw, &th);
    int lx = (128 - tw) / 2;
    int barY = 16 - 3 - 6;

    if (intensity > 0.7f) {
        display.fillRect(lx, barY, tw, 6, SSD1306_WHITE);
    } else if (intensity > 0.4f) {
        for (int px = lx; px < lx + (int)tw; px += 2)
            for (int py = barY; py < barY + 6; py += 2)
                if ((px + py) % 4 == 0) display.drawPixel(px, py, SSD1306_WHITE);
    } else {
        for (int px = lx; px < lx + (int)tw; px += 4)
            for (int py = barY; py < barY + 6; py += 4)
                if ((px + py) % 8 == 0) display.drawPixel(px, py, SSD1306_WHITE);
    }
}

// ── Task evaluation overlay ───────────────────────────────────────────────

void updateTaskEval() {
    if (app.eval.mode == EVAL_DISPLAYING) {
        if (millis() - app.eval.dispStart >= EVAL_DISPLAY_MS) {
            app.eval.mode = EVAL_INACTIVE;
        }
    }
}

static void drawTaskEval() {
    if (app.eval.mode == EVAL_HOLDING) {
        uint32_t held = millis() - app.eval.holdStart;
        float progress = constrain((float)held / EVAL_ANIM_MS, 0.0f, 1.0f);
        int radius = (int)(80.0f * progress);
        for (int py = 16; py < 64; py++) {
            for (int px = 0; px < 128; px++) {
                int dx = px - 64, dy = py - 40;
                if ((int)sqrtf((float)(dx*dx + dy*dy)) <= radius) {
                    display.drawPixel(px, py, SSD1306_WHITE);
                }
            }
        }
    } else if (app.eval.mode == EVAL_DISPLAYING) {
        display.fillRect(0, 16, 128, 48, SSD1306_BLACK);
        display.setTextSize(1);
        display.setTextWrap(true);
        const char* msg = EVAL_MESSAGES[app.eval.msgIndex];
        int16_t x1, y1; uint16_t tw, th;
        display.getTextBounds(msg, 0, 0, &x1, &y1, &tw, &th);
        display.setCursor((128 - tw) / 2, 16 + (48 - th) / 2);
        display.print(msg);
        display.setTextWrap(false);
    }
}

// ── Home screen ───────────────────────────────────────────────────────────

static void drawHome() {
    if (app.eval.mode != EVAL_INACTIVE) {
        drawTaskEval();
        return;
    }
    char lvBuf[4];
    snprintf(lvBuf, sizeof(lvBuf), "%02d", app.game.level);
    display.setTextSize(4);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(lvBuf, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor((128 - tw) / 2, 16);
    display.print(lvBuf);
    drawExpBar();
    drawButtonCounter();
    drawLevelUp();
}

// ── Task initiation screen ────────────────────────────────────────────────

static void drawTaskInit() {
    display.setTextSize(3);
    display.setCursor(20, 20);
    if (app.taskInit.running) {
        uint32_t elapsed = millis() - app.taskInit.startTime;
        if (elapsed >= TASK_INIT_MS) {
            // Timer expired — mark done (audio was fired in input handler)
            app.taskInit.running  = false;
            app.taskInit.finished = true;
            display.print("00:00");
        } else {
            uint32_t rem = TASK_INIT_MS - elapsed;
            char buf[6];
            snprintf(buf, sizeof(buf), "%02lu:%02lu", rem / 60000, (rem % 60000) / 1000);
            display.print(buf);
        }
    } else if (app.taskInit.finished) {
        if ((millis() / 500) % 2 == 0) display.print("00:00");
    } else {
        display.print("02:00");
    }
}

// ── Task priority screen ──────────────────────────────────────────────────

static void formatHMS(char* buf, int sz, uint32_t ms) {
    uint32_t h = ms / 3600000;
    uint32_t m = (ms % 3600000) / 60000;
    uint32_t s = (ms % 60000) / 1000;
    snprintf(buf, sz, "%02lu:%02lu:%02lu", h, m, s);
}

static void drawTaskPrio() {
    display.setTextSize(1);
    char timeBuf[10];

    uint32_t elapsed = app.taskPrio.running ? millis() - app.taskPrio.startTime : 0;
    formatHMS(timeBuf, sizeof(timeBuf), elapsed);

    if (app.taskPrio.firstUseToday) {
        // No logs yet — show big centered timer
        display.setTextSize(2);
        int16_t x1, y1; uint16_t tw, th;
        display.getTextBounds(timeBuf, 0, 0, &x1, &y1, &tw, &th);
        bool visible = app.taskPrio.running || (millis() / 1000) % 2 == 0;
        if (visible) { display.setCursor((128 - tw) / 2, 20); display.print(timeBuf); }
        display.setTextSize(1);
    } else {
        // Has logs: compact timer bottom-right, log boxes above
        bool visible = app.taskPrio.running || (millis() / 1000) % 2 == 0;
        if (visible) { display.setCursor(60, 56); display.print(timeBuf); }

        for (int i = 0; i < app.taskPrio.logCount && i < 3; i++) {
            int boxY = 16 + i * 12;
            display.drawRect(0, boxY, 128, 11, SSD1306_WHITE);
            char logBuf[10];
            formatHMS(logBuf, sizeof(logBuf), app.taskPrio.logs[i]);
            display.setCursor(3, boxY + 2);
            display.print(logBuf);

            float pct = (float)app.taskPrio.logs[i] / (float)WORKDAY_MS * 100.0f;
            char pctBuf[8];
            snprintf(pctBuf, sizeof(pctBuf), "=%.1f%%", pct);
            display.setCursor(70, boxY + 2);
            display.print(pctBuf);
        }
    }
}

// ── Heatmap screen ────────────────────────────────────────────────────────

static void drawHeatmap() {
    const int boxSz  = 6, spacing = 2;
    const int totalW = (boxSz + spacing) * HEATMAP_COLS - spacing;
    const int startX = (128 - totalW) / 2;
    const int startY = 20;

    bool drawFilled = true;
    if (app.heatmap.flashActive) {
        uint32_t elapsed = millis() - app.heatmap.flashStart;
        if (elapsed >= HEATMAP_FLASH_MS) { app.heatmap.flashActive = false; }
        else {
            float prog = (float)elapsed / HEATMAP_FLASH_MS;
            drawFilled = ((int)(prog * HEATMAP_FLASH_CYCLES * 2) % 2 == 0);
        }
    }

    for (int row = 0; row < HEATMAP_ROWS; row++) {
        for (int col = 0; col < HEATMAP_COLS; col++) {
            int idx = row * HEATMAP_COLS + col;
            int x   = startX + col * (boxSz + spacing);
            int y   = startY + row * (boxSz + spacing);
            if (app.heatmap.grid[idx] && drawFilled)
                display.fillRect(x, y, boxSz, boxSz, SSD1306_WHITE);
            else
                display.drawRect(x, y, boxSz, boxSz, SSD1306_WHITE);
        }
    }
}

// ── Stopwatch limit notification ──────────────────────────────────────────

static void drawSwLimitNotif() {
    const int bx = 2, by = 8, bw = 124, bh = 48;
    display.fillRect(bx, by, bw, bh, SSD1306_BLACK);
    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    const char* lines[] = { "Stopwatch limit", "exceeded.", "Cut-off engaged." };
    for (int i = 0; i < 3; i++) {
        int16_t x1, y1; uint16_t tw, th;
        display.getTextBounds(lines[i], 0, 0, &x1, &y1, &tw, &th);
        display.setCursor(bx + (bw - tw) / 2, by + 10 + i * 13);
        display.print(lines[i]);
    }
}

// ── Boot screen ───────────────────────────────────────────────────────────

void showBootScreen() {
    // "MNEME" centred, bar below it — measure text to centre precisely
    display.setTextSize(2);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds("MNEME", 0, 0, &x1, &y1, &tw, &th);
    const int tx = (128 - tw) / 2;
    const int ty = 18;

    const int bw = 80, bh = 5;
    const int bx = (128 - bw) / 2;
    const int by = ty + th + 12;

    for (int i = 0; i <= bw; i++) {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(tx, ty);
        display.print("MNEME");
        display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
        display.fillRect(bx, by, i,  bh, SSD1306_WHITE);
        display.display();
        delay(2000 / bw);
    }
}

// ── Master display update ─────────────────────────────────────────────────

void updateDisplay() {
    display.clearDisplay();

    if (app.swLimitNotif) {
        if (millis() - app.swLimitNotifStart >= SW_NOTIF_MS) {
            app.swLimitNotif = false;
        } else {
            drawBatteryBar();
            drawSwLimitNotif();
            display.display();
            return;
        }
    }

    drawBatteryBar();

    switch (app.screen) {
        case SCREEN_HOME:       drawHome();     break;
        case SCREEN_TASK_INIT:  drawTaskInit(); break;
        case SCREEN_TASK_PRIO:  drawTaskPrio(); break;
        case SCREEN_HEATMAP:    drawHeatmap();  break;
        default: break;
    }

    display.display();
}
