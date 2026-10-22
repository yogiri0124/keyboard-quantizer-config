/* Host-test stand-in for quantum/vial.h. */
#pragma once

#include <stdint.h>

#define VIAL_MACRO_EXT_TAP 5
#define VIAL_MACRO_EXT_DOWN 6
#define VIAL_MACRO_EXT_UP 7
#define VIAL_MATRIX_MAGIC 240

typedef struct {
    uint16_t on_tap;
    uint16_t on_hold;
    uint16_t on_double_tap;
    uint16_t on_tap_hold;
    uint16_t custom_tapping_term;
} vial_tap_dance_entry_t;

void vial_keycode_tap(uint16_t keycode);
void vial_keycode_down(uint16_t keycode);
void vial_keycode_up(uint16_t keycode);
