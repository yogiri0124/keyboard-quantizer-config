/* Host-test stand-in for tmk_core/protocol/report.h. */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int8_t  v;
    int8_t  h;
} report_mouse_t;
