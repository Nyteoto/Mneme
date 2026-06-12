#pragma once
#include <stdint.h>

// Bring up Serial2 + the DTR flow-control pin. Call once from setup().
void printerBegin();

// Renders a channel's section history to the QR204 over Serial2 as ESC/POS
// text, honouring the current PrintConfig and respecting DTR. Output is also
// echoed to USB Serial for debugging. (Raster image of the timeline bars that
// mirrors the OLED preview is the next step — see printChannel TODO.)
void printChannel(uint8_t idx);
