#pragma once
#include <stdint.h>

// Renders a channel's section history as a printable strip. Until the thermal
// module is wired this emits to Serial in the same shape the paper will take,
// honouring the current PrintConfig. Swap the body for the UART driver later.
void printChannel(uint8_t idx);
