/* Host-test stand-in for tmk_core/protocol/report.h. */
#pragma once

#include <stdint.h>

enum { MOUSE_BTN1 = 1 << 0, MOUSE_BTN2 = 1 << 1, MOUSE_BTN3 = 1 << 2 };

typedef struct {
    uint8_t buttons;
    int16_t x;
    int16_t y;
#ifdef WHEEL_EXTENDED_REPORT
    int16_t v;
    int16_t h;
#else
    int8_t v;
    int8_t h;
#endif
} __attribute__((packed)) report_mouse_t; /* as QMK's: no pointers into it */
