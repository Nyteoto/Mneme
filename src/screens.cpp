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
static void drawNavBar() {
    const int navY   = 11;
    const int pitch  = 13;
    const int totalW = NUM_CHANNELS * pitch;
    const int startX = (128 - totalW) / 2;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        int cx = startX + i * pitch + pitch / 2;
        if (i == app.currentChannel) {
            display.fillRect(cx - 4, navY, 9, 7, SSD1306_WHITE);
        } else if (channelUsed(app.channels[i])) {
            display.fillRect(cx - 1, navY + 2, 3, 3, SSD1306_WHITE);
        } else {
            display.drawPixel(cx, navY + 3, SSD1306_WHITE);
        }
    }
    display.drawFastHLine(0, 19, 128, SSD1306_WHITE);   // top band divider (y0-19)
}

// ── Current-section timeline ──────────────────────────────────────────────
// One horizontal bar spanning a ~3-month window. The past is NOT drawn — only
// the current section grows left→right, with a chevron pinned to "now" at the
// leading edge. All other metadata lives on the printout, not the screen.
static void drawTimeline(const Channel& c) {
    const int x0 = 6, barW = 116;     // VIS_WINDOW_DAYS spans x0 .. x0+barW
    const int barY = 58, barH = 5;    // pinned to the bottom edge

    // 3-month baseline scale
    display.drawFastHLine(x0, barY + barH, barW + 1, SSD1306_WHITE);

    uint32_t cur   = channelCurrentSectionDays(c);
    uint32_t shown = cur > VIS_WINDOW_DAYS ? VIS_WINDOW_DAYS : cur;
    int fillW = (int)((uint32_t)barW * shown / VIS_WINDOW_DAYS);
    if (fillW < 2) fillW = 2;          // always show a nub on day 0
    display.fillRect(x0, barY, fillW, barH, SSD1306_WHITE);

    // chevron marks the latest date (the leading edge of the live section)
    int tipX = x0 + fillW;
    if (tipX > x0 + barW) tipX = x0 + barW;
    display.fillTriangle(tipX - 3, barY - 6, tipX + 3, barY - 6, tipX, barY - 1, SSD1306_WHITE);
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
