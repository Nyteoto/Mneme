#include "storage.h"
#include "config.h"
#include "state.h"
#include <EEPROM.h>

void storageInit() {
    EEPROM.begin(EEPROM_SIZE);

    // If the magic matches but the version is stale, the layout has changed and
    // every field would land at the wrong offset. Wipe so loadData() starts clean.
    uint16_t magic; uint8_t ver;
    EEPROM.get(ADDR_MAGIC,   magic);
    EEPROM.get(ADDR_VERSION, ver);
    if (magic == EEPROM_MAGIC && ver != EEPROM_VERSION) {
        for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
        EEPROM.commit();
    }
}

void saveData() {
    const uint16_t magic = EEPROM_MAGIC;
    const uint8_t  ver   = EEPROM_VERSION;
    EEPROM.put(ADDR_MAGIC,    magic);
    EEPROM.put(ADDR_VERSION,  ver);
    EEPROM.put(ADDR_VOLUME,   app.volume);
    EEPROM.put(ADDR_PRINTCFG, app.print);

    int addr = ADDR_CHANNELS;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        EEPROM.put(addr, app.channels[i]);
        addr += sizeof(Channel);
    }

    EEPROM.commit();
    app.dirty        = false;
    app.lastSaveTime = millis();
}

bool loadData() {
    uint16_t magic; uint8_t ver;
    EEPROM.get(ADDR_MAGIC,   magic);
    EEPROM.get(ADDR_VERSION, ver);
    if (magic != EEPROM_MAGIC || ver != EEPROM_VERSION) return false;

    float vol; EEPROM.get(ADDR_VOLUME, vol);
    if (vol >= 0.0f && vol <= 1.0f) app.volume = vol;

    EEPROM.get(ADDR_PRINTCFG, app.print);
    // Clamp print config to valid ranges in case of a partial/garbage read.
    if (app.print.fontSize < 1 || app.print.fontSize > 3) app.print.fontSize = 1;
    if (app.print.bold > 1)                                app.print.bold     = 0;
    if (app.print.barWidth < 1 || app.print.barWidth > 12) app.print.barWidth = 4;

    int addr = ADDR_CHANNELS;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        EEPROM.get(addr, app.channels[i]);
        // Defensive: a corrupt sectionCount would index out of bounds later.
        if (app.channels[i].sectionCount > MAX_SECTIONS) {
            app.channels[i] = Channel{};
        }
        addr += sizeof(Channel);
    }
    return true;
}
