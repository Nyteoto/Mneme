#include "print.h"
#include "config.h"
#include "state.h"
#include "channels.h"
#include "RTClib.h"
#include <Arduino.h>

// ── Low-level: DTR-gated byte stream to the QR204 ─────────────────────────
// The printer pulls DTR LOW when it can accept data and drives it HIGH while
// its buffer/head is busy. We wait for "ready" before each byte so we never
// overrun the small input buffer. A disconnected printer reads LOW (internal
// pulldown) and a stuck-HIGH line times out, so printing can't hang the device.
static void printerWaitReady() {
    uint32_t t0 = millis();
    while (digitalRead(PRINTER_DTR_PIN) == HIGH) {
        if (millis() - t0 > PRINTER_DTR_TIMEOUT_MS) break;
    }
}

static void ppByte(uint8_t b) {
    printerWaitReady();
    Serial2.write(b);
}

// Visible text → printer AND mirrored to USB Serial for debugging.
static void ppStr(const char* s) {
    for (const char* p = s; *p; p++) ppByte((uint8_t)*p);
    Serial.print(s);
}

// ── ESC/POS helpers ───────────────────────────────────────────────────────
static void ppCmd2(uint8_t a, uint8_t b)           { ppByte(a); ppByte(b); }
static void ppCmd3(uint8_t a, uint8_t b, uint8_t c){ ppByte(a); ppByte(b); ppByte(c); }

static void ppReset()        { ppCmd2(0x1B, '@'); }              // ESC @  init
static void ppBold(bool on)  { ppCmd3(0x1B, 'E', on ? 1 : 0); }  // ESC E n
static void ppCenter(bool c) { ppCmd3(0x1B, 'a', c ? 1 : 0); }   // ESC a n
static void ppFeed(uint8_t n){ ppCmd3(0x1B, 'd', n); }           // ESC d n  (tear-off)

// GS ! n — character magnification. size 1..3 → 1x..3x in both axes.
static void ppSize(uint8_t size) {
    if (size < 1) size = 1;
    if (size > 3) size = 3;
    uint8_t m = (uint8_t)(size - 1);
    ppCmd3(0x1D, '!', (uint8_t)((m << 4) | m));
}

static void ppLine(const char* s) { ppStr(s); ppByte('\n'); Serial.println(); }

// RTClib's DateTime epoch is 2000-01-01; our timestamps are unix (1970).
static void fmtDate(char* buf, int n, uint32_t unix) {
    if (unix < 946684800UL) { snprintf(buf, n, "----/--/--"); return; }
    DateTime d(unix - 946684800UL);
    snprintf(buf, n, "%04d/%02d/%02d", d.year(), d.month(), d.day());
}

void printerBegin() {
    pinMode(PRINTER_DTR_PIN, INPUT_PULLDOWN);   // floating/disconnected = "ready"
    Serial2.setTX(PRINTER_TX_PIN);
    Serial2.setRX(PRINTER_RX_PIN);
    Serial2.begin(PRINTER_BAUD);
}

// Emits the channel's history as a bare ESC/POS strip. Mneme prints the
// skeleton; the meaning is written by hand in the user's journal.
//
// TODO(raster): replace the ASCII bar block below with a 384-dot-wide
// monochrome buffer rendered via drawSectionBars() and pushed with GS v 0,
// so the paper mirrors the OLED print-preview (CH pill + big count + chevron).
void printChannel(uint8_t idx) {
    if (idx >= NUM_CHANNELS) return;
    const Channel& c = app.channels[idx];

    char title[24];
    snprintf(title, sizeof(title), "MNEME  //  CH %d", idx + 1);

    ppReset();
    ppCenter(true);
    ppSize(app.print.fontSize);
    ppBold(true);
    ppLine(title);
    ppBold(app.print.bold != 0);
    ppSize(1);
    ppCenter(false);
    ppLine("--------------------------------");

    if (!channelUsed(c)) {
        ppLine("  (no history yet)");
        ppLine("--------------------------------");
        ppFeed(3);
        return;
    }

    char startBuf[12], nowBuf[12], line[64];
    fmtDate(startBuf, sizeof(startBuf), c.startUnix);
    fmtDate(nowBuf,   sizeof(nowBuf),   nowUnix());

    snprintf(line, sizeof(line), "  started   %s", startBuf);            ppLine(line);
    snprintf(line, sizeof(line), "  today     %s", nowBuf);              ppLine(line);
    snprintf(line, sizeof(line), "  %lu days ongoing",
             (unsigned long)channelTotalDays(c));                       ppLine(line);
    snprintf(line, sizeof(line), "  %lu sections so far",
             (unsigned long)c.sectionCount);                            ppLine(line);
    ppLine("");

    // longest section sets the ASCII bar scale
    uint32_t maxDays = 1;
    for (uint32_t i = 0; i < c.sectionCount; i++) {
        uint32_t d = channelSectionDays(c, i);
        if (d > maxDays) maxDays = d;
    }

    for (uint32_t i = 0; i < c.sectionCount; i++) {
        uint32_t d = channelSectionDays(c, i);
        int bars = (int)((uint32_t)20 * d / maxDays);
        if (bars < 1) bars = 1;
        int off = snprintf(line, sizeof(line), "  S%-2lu %4lud  ",
                           (unsigned long)(i + 1), (unsigned long)d);
        for (int b = 0; b < bars && off < (int)sizeof(line) - 9; b++) line[off++] = '#';
        line[off] = '\0';
        if (i == c.sectionCount - 1) strncat(line, "  <- now", sizeof(line) - off - 1);
        ppLine(line);
    }

    ppLine("--------------------------------");
    ppFeed(3);   // advance past the tear bar (QR204 has no auto-cutter)
}
