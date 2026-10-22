/* Host-test stand-in for tmk_core/protocol/report.h. */
#pragma once

#include <stdint.h>

enum { MOUSE_BTN1 = 1 << 0, MOUSE_BTN2 = 1 << 1, MOUSE_BTN3 = 1 << 2 };

typedef struct {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int8_t  v;
    int8_t  h;
} report_mouse_t;
