#pragma once
#include "state.h"

// Returns the current RTC time as unix seconds (0 if the RTC is unreadable).
uint32_t nowUnix();

bool     channelUsed(const Channel& c);

// Running totals (in whole days). currentSection grows against "now".
uint32_t channelTotalDays(const Channel& c);
uint32_t channelCurrentSectionDays(const Channel& c);

// Length in days of section i (0-based); the live section is measured to now.
uint32_t channelSectionDays(const Channel& c, int i);

// Append a new section boundary at the current time (or start the channel on
// its first-ever press). Marks state dirty and persists.
void     channelMarkSection(uint8_t idx);

// Fill a spread of channels with fabricated history so the UI can be designed
// before any real data exists. Anchored to "now" so bars show varied lengths.
void     channelsInitDemo();

// Wipe every channel back to the unused state.
void     channelsClear();
