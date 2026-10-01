/* Copyright 2020 sekigon-gonnoc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#define VIAL_KEYBOARD_UID {0x05, 0xE4, 0xA1, 0x7F, 0xDC, 0x87, 0xCB, 0x2A}

/* Keys and buttons arrive as USB reports the device already debounced. QMK's
 * default (5 ms, sym_defer_g) would also restart on every cursor report, so
 * a click while the ball moves waited, and a press and release in successive
 * scans vanished. */
#define DEBOUNCE 0

#define MATRIX_COLS_DEFAULT 8
#define MATRIX_MSGES_ROW 31

#define MOUSE_EXTENDED_REPORT
/* High-resolution wheel (patches/0002). A host that enables it splits a
 * detent into this many counts; one that does not keeps x1. 120 matches the
 * Windows/Linux wheel unit exactly: one count is a wheel delta of 1. */
#define MOUSE_WHEEL_RESOLUTION_MULTIPLIER 120

#define DYNAMIC_KEYMAP_LAYER_COUNT 8
#define WEAR_LEVELING_BACKING_SIZE (8192 * 2)
#define WEAR_LEVELING_LOGICAL_SIZE 8192

#define VIAL_TAP_DANCE_ENTRIES 32
#define VIAL_COMBO_ENTRIES 32
#define VIAL_KEY_OVERRIDE_ENTRIES 32
#define DYNAMIC_KEYMAP_MACRO_COUNT 16
#define COMBO_TERM 100

#define VIA_FIRMWARE_VERSION 4
