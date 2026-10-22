/* Host-test stand-in for quantum/vial.h. */
#pragma once

#include <stdint.h>

#define VIAL_MACRO_EXT_TAP 5
#define VIAL_MACRO_EXT_DOWN 6
#define VIAL_MACRO_EXT_UP 7

void vial_keycode_tap(uint16_t keycode);
void vial_keycode_down(uint16_t keycode);
void vial_keycode_up(uint16_t keycode);
