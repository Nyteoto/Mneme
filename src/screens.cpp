#include "screens.h"
#include "config.h"
#include "state.h"
#include "channels.h"
#include "peripherals.h"
#include <Arduino.h>

// ── Shared helpers ────────────────────────────────────────────────────────

static void drawBatteryBar() {
    const int x = 0, y = 56, w = 20, h = 8;
    bool draw = true;
    if (app.pwr.flashActive) draw = (millis() / 200) % 2 == 0;
    if (!draw) return;

    display.drawRect(x, y, w, h, SSD1306_WHITE);
    display.fillRect(x + w, y + 2, 2, h - 4, SSD1306_WHITE);   // terminal nub

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

static void centeredText(const char* s, int y, int size) {
    display.setTextSize(size);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor((128 - (int)tw) / 2, y);
    display.print(s);
}

// ── Top navigation bar: 10 channel positions ──────────────────────────────
// Filled square = selected channel; small dot = used channel; bare tick =
// unused. This is the "channel toggle" — the rotary scrubs across it.
static void drawNavBar() {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        int cx = (i * 128) / NUM_CHANNELS + (128 / NUM_CHANNELS) / 2;
        if (i == app.currentChannel) {
            display.fillRect(cx - 4, 0, 9, 8, SSD1306_WHITE);
        } else if (channelUsed(app.channels[i])) {
            display.fillRect(cx - 1, 3, 3, 3, SSD1306_WHITE);
        } else {
            display.drawPixel(cx, 5, SSD1306_WHITE);
        }
    }
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}

// ── Section bar chart for the selected channel ────────────────────────────
static void drawBarChart(const Channel& c) {
    const int baseline = 52, plotTop = 24, plotH = baseline - plotTop;
    const int plotX0 = 2, plotW = 124;
    int n = (int)c.sectionCount;

    // longest section sets the vertical scale (min 1 to avoid /0)
    uint32_t maxDays = 1;
    for (int i = 0; i < n; i++) {
        uint32_t d = channelSectionDays(c, i);
        if (d > maxDays) maxDays = d;
    }

    int slot = plotW / n;
    int barW = slot - 2; if (barW > 16) barW = 16; if (barW < 2) barW = 2;

    bool blink = (millis() / 400) % 2 == 0;
    for (int i = 0; i < n; i++) {
        uint32_t d = channelSectionDays(c, i);
        int h = (int)((uint32_t)plotH * d / maxDays);
        if (h < 1) h = 1;
        int x = plotX0 + i * slot;
        int y = baseline - h;
        if (i == n - 1) {
            // current (ongoing) section — outlined, gently blinking fill
            display.drawRect(x, y, barW, h, SSD1306_WHITE);
            if (blink && h > 2) display.fillRect(x + 1, y + 1, barW - 2, h - 2, SSD1306_WHITE);
        } else {
            display.fillRect(x, y, barW, h, SSD1306_WHITE);
        }
    }
    display.drawFastHLine(plotX0, baseline + 1, plotW, SSD1306_WHITE);
}

// ── Main screen ────────────────────────────────────────────────────────────
static void drawMain() {
    const Channel& c = app.channels[app.currentChannel];

    // info row
    char buf[24];
    display.setTextSize(1);
    snprintf(buf, sizeof(buf), "CH%d", app.currentChannel + 1);
    display.setCursor(2, 13);
    display.print(buf);

    if (!channelUsed(c)) {
        centeredText("press to begin", 34, 1);
        return;
    }

    uint32_t total = channelTotalDays(c);
    uint32_t cur   = channelCurrentSectionDays(c);

    snprintf(buf, sizeof(buf), "%lud", (unsigned long)total);
    display.setTextSize(1);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(64 - (int)tw / 2, 13);
    display.print(buf);

    snprintf(buf, sizeof(buf), "S%lu %lud",
             (unsigned long)c.sectionCount, (unsigned long)cur);
    display.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(126 - (int)tw, 13);
    display.print(buf);

    drawBarChart(c);
}

// ── "Section off?" confirm modal ──────────────────────────────────────────
static void drawConfirm() {
    const int bx = 6, by = 14, bw = 116, bh = 40;
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
    const int bx = 18, by = 13, bw = 92, bh = 42;
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

// ── Print settings editor with a tiny live preview ─────────────────────────
static void drawSettings() {
    display.setTextSize(1);
    char buf[20];
    const char* labels[SET_COUNT] = { "Font", "Bold", "BarW", "Done" };
    for (int i = 0; i < SET_COUNT; i++) {
        int rowY = 13 + i * 10;
        bool sel = (i == app.settingField);
        if (sel) { display.fillRect(0, rowY - 1, 70, 9, SSD1306_WHITE);
                   display.setTextColor(SSD1306_BLACK); }
        else      display.setTextColor(SSD1306_WHITE);
        display.setCursor(2, rowY);
        switch (i) {
            case SET_FONT: snprintf(buf, sizeof(buf), "Font  %d", app.print.fontSize); break;
            case SET_BOLD: snprintf(buf, sizeof(buf), "Bold  %s", app.print.bold ? "on" : "off"); break;
            case SET_BARW: snprintf(buf, sizeof(buf), "BarW  %d", app.print.barWidth); break;
            default:       snprintf(buf, sizeof(buf), "%s", labels[i]); break;
        }
        display.print(buf);
    }
    display.setTextColor(SSD1306_WHITE);

    // preview pane: three mock bars rendered with the current bar width
    const int px = 80, baseline = 52;
    display.drawRect(74, 12, 52, 44, SSD1306_WHITE);
    int heights[3] = {12, 26, 18};
    for (int i = 0; i < 3; i++) {
        int x = px + i * (app.print.barWidth + 3);
        display.fillRect(x, baseline - heights[i], app.print.barWidth, heights[i], SSD1306_WHITE);
    }
    display.setTextSize(app.print.fontSize > 2 ? 2 : app.print.fontSize);
    display.setCursor(78, 14);
    display.print("86d");
    display.setTextSize(1);
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
