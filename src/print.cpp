#include "print.h"
#include "config.h"
#include "state.h"
#include "channels.h"
#include "RTClib.h"
#include <Arduino.h>

// RTClib's DateTime epoch is 2000-01-01; our timestamps are unix (1970).
static void fmtDate(char* buf, int n, uint32_t unix) {
    if (unix < 946684800UL) { snprintf(buf, n, "----/--/--"); return; }
    DateTime d(unix - 946684800UL);
    snprintf(buf, n, "%04d/%02d/%02d", d.year(), d.month(), d.day());
}

// Stub thermal output. The strip is intentionally bare — Mneme prints the
// skeleton; the meaning is written by hand in the user's journal.
void printChannel(uint8_t idx) {
    if (idx >= NUM_CHANNELS) return;
    const Channel& c = app.channels[idx];

    Serial.println();
    Serial.println(F("--------------------------------"));
    Serial.printf("  MNEME  //  CHANNEL %d\n", idx + 1);
    Serial.println(F("--------------------------------"));

    if (!channelUsed(c)) {
        Serial.println(F("  (no history yet)"));
        Serial.println(F("--------------------------------"));
        Serial.println();
        return;
    }

    char startBuf[12], nowBuf[12];
    fmtDate(startBuf, sizeof(startBuf), c.startUnix);
    fmtDate(nowBuf,   sizeof(nowBuf),   nowUnix());

    Serial.printf("  started   %s\n", startBuf);
    Serial.printf("  today     %s\n", nowBuf);
    Serial.printf("  %lu days ongoing\n", (unsigned long)channelTotalDays(c));
    Serial.printf("  %lu sections so far\n\n", (unsigned long)c.sectionCount);

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
        Serial.printf("  S%-2lu %4lud  ", (unsigned long)(i + 1), (unsigned long)d);
        for (int b = 0; b < bars; b++) Serial.print('#');
        if (i == c.sectionCount - 1) Serial.print("  <- now");
        Serial.println();
    }

    Serial.println(F("--------------------------------"));
    Serial.printf("  font %d  bold %s  barW %d\n",
                  app.print.fontSize, app.print.bold ? "on" : "off", app.print.barWidth);
    Serial.println();
}
