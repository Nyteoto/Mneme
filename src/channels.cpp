#include "channels.h"
#include "config.h"
#include "peripherals.h"
#include "storage.h"

uint32_t nowUnix() {
    DateTime n = rtc.now();
    uint32_t u = n.unixtime();
    // RTClib returns 0 / garbage if the chip is missing; guard the math.
    return (u > 0) ? u : 0;
}

bool channelUsed(const Channel& c) {
    return c.startUnix != 0 && c.sectionCount > 0;
}

uint32_t channelTotalDays(const Channel& c) {
    if (!channelUsed(c)) return 0;
    uint32_t now = nowUnix();
    if (now <= c.startUnix) return 0;
    return (now - c.startUnix) / SECS_PER_DAY;
}

uint32_t channelSectionDays(const Channel& c, int i) {
    if (i < 0 || (uint32_t)i >= c.sectionCount) return 0;
    uint32_t begin = c.sectionStart[i];
    uint32_t end   = ((uint32_t)(i + 1) < c.sectionCount)
                       ? c.sectionStart[i + 1]
                       : nowUnix();
    if (end <= begin) return 0;
    return (end - begin) / SECS_PER_DAY;
}

uint32_t channelCurrentSectionDays(const Channel& c) {
    if (!channelUsed(c)) return 0;
    return channelSectionDays(c, (int)c.sectionCount - 1);
}

void channelMarkSection(uint8_t idx) {
    if (idx >= NUM_CHANNELS) return;
    Channel& c = app.channels[idx];
    uint32_t now = nowUnix();

    if (!channelUsed(c)) {
        // First-ever press: the domain begins now, section 1 opens.
        c.startUnix       = now;
        c.sectionStart[0] = now;
        c.sectionCount    = 1;
    } else if (c.sectionCount < MAX_SECTIONS) {
        c.sectionStart[c.sectionCount++] = now;
    } else {
        return;   // section log full — ignore silently for now
    }

    app.dirty = true;
    saveData();
}

void channelsClear() {
    for (int i = 0; i < NUM_CHANNELS; i++) app.channels[i] = Channel{};
}

// ── Demo prefill ───────────────────────────────────────────────────────────
// Day-offsets (days ago) at which each fabricated section opened. The first
// entry is the channel's start; later entries are progression marks. Lengths
// are deliberately varied — rapid bursts, long plateaus, fresh single bars —
// so the chart UI can be exercised against realistic rhythm shapes.
namespace {
struct DemoSpec { int count; int daysAgo[6]; };
const DemoSpec DEMO[NUM_CHANNELS] = {
    { 4, {86, 65, 36, 14} },         // ch0: current section = 14d
    { 3, {40, 28, 10} },             // ch1: current section = 10d
    { 4, {213, 150, 60, 12} },       // ch2: current section = 12d
    { 1, {7} },                      // ch3: freshly started, 7d
    { 5, {120, 118, 116, 100, 40} }, // ch4: current section = 40d
    { 0, {} },                       // ch5: unused
    { 0, {} },                       // ch6: unused
    { 0, {} },                       // ch7: unused
};
}

void channelsInitDemo() {
    uint32_t anchor = nowUnix();
    for (int i = 0; i < NUM_CHANNELS; i++) {
        Channel& c = app.channels[i];
        c = Channel{};
        const DemoSpec& d = DEMO[i];
        if (d.count == 0) continue;
        for (int s = 0; s < d.count && s < MAX_SECTIONS; s++) {
            c.sectionStart[s] = anchor - (uint32_t)d.daysAgo[s] * SECS_PER_DAY;
        }
        c.startUnix    = c.sectionStart[0];
        c.sectionCount = d.count;
    }
}
