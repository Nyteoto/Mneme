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
// 50% checkerboard fill — gives completed sections a lighter "past" texture.
static void fillDither(int x, int y, int w, int h) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            if (((x + xx) + (y + yy)) & 1)
                display.drawPixel(x + xx, y + yy, SSD1306_WHITE);
}

// Ordered-dither fill at a given density (level 0..16) via a 4x4 Bayer matrix.
// Used to animate the current section's bit-density for a smooth "breathing" blink.
static void fillDensity(int x, int y, int w, int h, int level) {
    static const uint8_t BAYER4[4][4] = {
        {  0,  8,  2, 10 },
        { 12,  4, 14,  6 },
        {  3, 11,  1,  9 },
        { 15,  7, 13,  5 }
    };
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            if (BAYER4[(y + yy) & 3][(x + xx) & 3] < level)
                display.drawPixel(x + xx, y + yy, SSD1306_WHITE);
}

// Slow breathe: density oscillates ~50%..100% on a gentle triangle (~2s period).
static int currentBlinkLevel() {
    const uint32_t period = 2000;
    float phase = (millis() % period) / (float)period;     // 0..1
    float tri   = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;  // 0..1..0
    return 8 + (int)(tri * 8.0f + 0.5f);                   // 8..16
}

// Contiguous section bars, left-anchored at x0. The drawn length grows with the
// channel's age at a constant scale (maxW px == VIS_WINDOW_DAYS days): a young
// channel is a short bar at the edge that elongates over time, capping at maxW
// (= 2/3 of the screen). Once full it rolls — older sections scroll off the left
// while the chevron (now) holds at the cap. Current section solid, past dithered.
static void drawSectionBars(const Channel& c, int x0, int maxW, int barY, int barH) {
    const int GAP = 3;                  // divider between stacked sections
    uint32_t now = nowUnix();
    if (now == 0) return;

    uint32_t winSecs   = (uint32_t)VIS_WINDOW_DAYS * SECS_PER_DAY;
    uint32_t totalSecs = (now > c.startUnix) ? now - c.startUnix : 0;
    uint32_t visSecs   = totalSecs < winSecs ? totalSecs : winSecs;   // grows, then caps
    uint32_t winStart  = now - visSecs;

    for (uint32_t i = 0; i < c.sectionCount; i++) {
        uint32_t segStart = c.sectionStart[i];
        uint32_t segEnd   = (i + 1 < c.sectionCount) ? c.sectionStart[i + 1] : now;
        if (segEnd <= winStart) continue;                  // scrolled off the left
        if (segStart < winStart) segStart = winStart;

        int xa = x0 + (int)((uint64_t)maxW * (segStart - winStart) / winSecs);
        int xb = x0 + (int)((uint64_t)maxW * (segEnd   - winStart) / winSecs);
        int w  = xb - xa - GAP;
        if (w < 1) w = 1;
        if (i == c.sectionCount - 1) fillDensity(xa, barY, w, barH, currentBlinkLevel()); // current: breathing
        else                         fillDither(xa, barY, w, barH);                        // past: dithered
    }

    int nowX = x0 + (int)((uint64_t)maxW * visSecs / winSecs);   // grows to x0+maxW, then holds
    display.fillTriangle(nowX - 3, barY - 5, nowX + 3, barY - 5, nowX, barY - 1, SSD1306_WHITE);
}

static void drawTimeline(const Channel& c) {
    // left-anchored at x2; caps at ~2/3 of the 128px screen (x2..x86)
    drawSectionBars(c, 2, 84, 58, 5);
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

// ── "Clear history?" confirm modal ─────────────────────────────────────────
static void drawClearConfirm() {
    const int bx = 6, by = 22, bw = 116, bh = 40;
    display.fillRect(bx, by, bw, bh, SSD1306_BLACK);
    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    char buf[24];
    snprintf(buf, sizeof(buf), "Clear CH%d history?", app.currentChannel + 1);
    centeredText(buf, by + 6, 1);
    centeredText("press = confirm", by + 18, 1);
    centeredText("turn  = cancel",  by + 28, 1);
}

// ── Long-hold action menu ──────────────────────────────────────────────────
static void drawMenu() {
    const int bx = 18, by = 20, bw = 92, bh = 43;
    display.fillRect(bx, by, bw, bh, SSD1306_BLACK);
    display.drawRect(bx, by, bw, bh, SSD1306_WHITE);
    const char* items[MENU_COUNT] = { "Print", "Settings", "Clear", "Cancel" };
    for (int i = 0; i < MENU_COUNT; i++) {
        int rowY = by + 3 + i * 10;
        if (i == app.menuSel) {
            display.fillRect(bx + 2, rowY - 1, bw - 4, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        display.setTextSize(1);
        display.setCursor(bx + 8, rowY);
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
            if (app.ui == UI_CONFIRM)       drawConfirm();
            if (app.ui == UI_MENU)          drawMenu();
            if (app.ui == UI_CLEAR_CONFIRM) drawClearConfirm();
            break;
    }

    display.display();
}
