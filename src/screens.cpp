#include "screens.h"
#include "config.h"
#include "state.h"
#include "channels.h"
#include "peripherals.h"
#include <Arduino.h>

// ── Shared helpers ────────────────────────────────────────────────────────

// Battery lives in the top band now (top-right), so the whole area below the
// y19 divider belongs to the main readout.
static void drawBatteryBar() {
    const int w = 16, h = 7, y = 0;
    bool draw = true;
    if (app.pwr.flashActive) draw = (millis() / 200) % 2 == 0;

    char buf[6];
    if (app.pwr.source == POWER_USB) snprintf(buf, sizeof(buf), "USB");
    else                             snprintf(buf, sizeof(buf), "%d%%", app.pwr.stablePct);

    display.setTextSize(1);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
    int textX = 127 - (int)tw;
    int bx    = textX - 4 - (w + 2);

    if (draw) {
        display.drawRect(bx, y, w, h, SSD1306_WHITE);
        display.fillRect(bx + w, y + 2, 2, h - 4, SSD1306_WHITE);   // terminal nub
        int fill = (w - 2) * app.pwr.stablePct / 100;
        if (fill > 0) display.fillRect(bx + 1, y + 1, fill, h - 2, SSD1306_WHITE);
    }
    display.setCursor(textX, y);
    display.print(buf);
}

static void centeredText(const char* s, int y, int size) {
    display.setTextSize(size);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor((128 - (int)tw) / 2, y);
    display.print(s);
}

// ── Top navigation bar: channel positions, centered ───────────────────────
// Filled square = selected channel; small dot = used channel; bare pixel =
// unused. This is the "channel toggle" — the rotary scrubs across it.
// Channel indicator: a single labeled bar at top-left, level with the battery.
static void drawNavBar() {
    char buf[8];
    snprintf(buf, sizeof(buf), "CH %d", app.currentChannel + 1);
    display.setTextSize(1);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);

    const int padX = 3;
    const int bw = (int)tw + padX * 2, bh = 9;
    display.fillRect(0, 0, bw, bh, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(padX, 1);
    display.print(buf);
    display.setTextColor(SSD1306_WHITE);
}

// ── Current-section timeline ──────────────────────────────────────────────
// Contiguous section bars within the rolling VIS_WINDOW_DAYS window ending at
// "now". Each section is a block whose width = its duration; new sections stack
// to the right onto the still-visible older ones; sections older than the
// window scroll off the left. A chevron pins the latest date at the right edge.
static void drawSectionBars(const Channel& c, int x0, int W, int barY, int barH) {
    uint32_t now = nowUnix();
    if (now == 0) return;
    uint32_t winDur   = (uint32_t)VIS_WINDOW_DAYS * SECS_PER_DAY;
    uint32_t winStart = (now > winDur) ? now - winDur : 0;

    for (uint32_t i = 0; i < c.sectionCount; i++) {
        uint32_t segStart = c.sectionStart[i];
        uint32_t segEnd   = (i + 1 < c.sectionCount) ? c.sectionStart[i + 1] : now;
        if (segEnd <= winStart || segStart >= now) continue;   // outside window
        if (segStart < winStart) segStart = winStart;
        if (segEnd   > now)      segEnd   = now;

        int xa = x0 + (int)((uint64_t)W * (segStart - winStart) / winDur);
        int xb = x0 + (int)((uint64_t)W * (segEnd   - winStart) / winDur);
        int w  = xb - xa - 1;          // 1px gap so stacked bars stay distinct
        if (w < 1) w = 1;
        display.fillRect(xa, barY, w, barH, SSD1306_WHITE);
    }

    int tipX = x0 + W;                  // chevron = now, at the right edge
    display.fillTriangle(tipX - 3, barY - 5, tipX + 3, barY - 5, tipX, barY - 1, SSD1306_WHITE);
}

static void drawTimeline(const Channel& c) {
    drawSectionBars(c, 6, 116, 58, 5);  // bottom edge of the main screen
}

// ── Main screen — the elapsed-section count is the hero, timeline below ────
static void drawMain() {
    const Channel& c = app.channels[app.currentChannel];

    if (!channelUsed(c)) {
        centeredText("press to begin", 36, 1);
        return;
    }

    // zero-padded day count, no unit — "07", "64", "213"
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu", (unsigned long)channelCurrentSectionDays(c));
    centeredText(buf, 20, 4);
    drawTimeline(c);
}

// ── "Section off?" confirm modal ──────────────────────────────────────────
static void drawConfirm() {
    const int bx = 6, by = 22, bw = 116, bh = 40;
    display.fillRect(bx, by, bw, bh, SSD1306_BLACK);
    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    char buf[24];
    bool used = channelUsed(app.channels[app.currentChannel]);
    snprintf(buf, sizeof(buf), used ? "Section off CH%d?" : "Begin CH%d?",
             app.currentChannel + 1);
    centeredText(buf, by + 6, 1);
    centeredText("press = confirm", by + 18, 1);
    centeredText("turn  = cancel",  by + 28, 1);
}

// ── Long-hold action menu ──────────────────────────────────────────────────
static void drawMenu() {
    const int bx = 18, by = 22, bw = 92, bh = 40;
    display.fillRect(bx, by, bw, bh, SSD1306_BLACK);
    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    const char* items[MENU_COUNT] = { "Print", "Settings", "Cancel" };
    for (int i = 0; i < MENU_COUNT; i++) {
        int rowY = by + 4 + i * 12;
        if (i == app.menuSel) {
            display.fillRect(bx + 2, rowY - 1, bw - 4, 11, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        display.setTextSize(1);
        display.setCursor(bx + 8, rowY + 1);
        display.print(items[i]);
    }
    display.setTextColor(SSD1306_WHITE);
}

// ── Print settings editor — live preview of the 58x30mm paper cut ──────────
// The box is drawn at the true 58:30 aspect of the receipt cut. Content is
// inset to the 48mm printable width (5mm margins on 58mm stock) and mirrors
// the on-screen UI: CH pill, the big elapsed-section count, timeline + chevron.
static void drawSettings() {
    const int   bx = 15, by = 1, bw = 97, bh = 50;   // 58x30mm @ ~1.67 px/mm
    const float s  = (float)bw / 58.0f;
    auto PX = [&](float mm) { return (int)(mm * s + 0.5f); };

    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);  // paper edge

    const Channel& c = app.channels[app.currentChannel];
    const int contentL = bx + PX(5);                  // 48mm printable region
    const int contentR = bx + PX(53);

    char buf[12];
    int16_t x1, y1; uint16_t tw, th;

    // CH pill (top-left of the printable area)
    snprintf(buf, sizeof(buf), "CH %d", app.currentChannel + 1);
    display.setTextSize(1);
    display.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
    display.fillRect(contentL, by + PX(2), (int)tw + 4, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(contentL + 2, by + PX(2) + 1);
    display.print(buf);
    display.setTextColor(SSD1306_WHITE);

    // hero count — size = Font, double-struck when Bold is on
    snprintf(buf, sizeof(buf), "%02lu", (unsigned long)channelCurrentSectionDays(c));
    int fs = app.print.fontSize; if (fs < 1) fs = 1; if (fs > 3) fs = 3;
    int numY = by + PX(11);
    display.setTextSize(fs);
    display.setCursor(contentL, numY);
    display.print(buf);
    if (app.print.bold) { display.setCursor(contentL + 1, numY); display.print(buf); }
    display.setTextSize(1);

    // timeline bar + chevron along the bottom of the printable area
    int barTh = app.print.barWidth; if (barTh < 1) barTh = 1; if (barTh > 6) barTh = 6;
    int barY  = by + bh - 4 - barTh;
    drawSectionBars(c, contentL, contentR - contentL, barY, barTh);

    // ── compact field row at the bottom ──
    char f0[8], f1[8], f2[8];
    snprintf(f0, sizeof(f0), "Fnt%d", app.print.fontSize);
    snprintf(f1, sizeof(f1), "Bld%d", app.print.bold);
    snprintf(f2, sizeof(f2), "Bar%d", app.print.barWidth);
    const char* fields[SET_COUNT] = { f0, f1, f2, "Done" };
    int fx = 4;
    display.setTextSize(1);
    for (int i = 0; i < SET_COUNT; i++) {
        display.getTextBounds(fields[i], 0, 0, &x1, &y1, &tw, &th);
        if (i == app.settingField) {
            display.fillRect(fx - 1, 54, (int)tw + 2, 9, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(fx, 55);
        display.print(fields[i]);
        display.setTextColor(SSD1306_WHITE);
        fx += (int)tw + 5;
    }
}

// ── Printing splash (stub until the thermal module is wired) ───────────────
static void drawPrinting() {
    char buf[20];
    snprintf(buf, sizeof(buf), "Printing CH%d", app.currentChannel + 1);
    centeredText(buf, 26, 1);
    centeredText("...", 40, 1);
}

// ── Boot screen (kept from the legacy build) ──────────────────────────────
void showBootScreen() {
    display.setTextSize(2);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds("MNEME", 0, 0, &x1, &y1, &tw, &th);
    const int tx = (128 - (int)tw) / 2;
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
    display.setTextColor(SSD1306_WHITE);

    switch (app.ui) {
        case UI_SETTINGS:
            drawSettings();
            break;
        case UI_PRINTING:
            drawPrinting();
            break;
        default:
            // MAIN / CONFIRM / MENU all share the nav bar + battery context
            drawNavBar();
            drawMain();
            drawBatteryBar();
            if (app.ui == UI_CONFIRM) drawConfirm();
            if (app.ui == UI_MENU)    drawMenu();
            break;
    }

    display.display();
}
