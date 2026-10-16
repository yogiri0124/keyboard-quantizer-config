// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* The identity keymap: HID usage u is the Vial cell at row u / 8 + 1,
 * column u % 8 (mini/hid_input.c fills the matrix that way), so the default
 * keymap sends every key as itself and Vial remaps by usage. Row 31 holds
 * the eight gesture cells, which no device sets. */

#pragma once

#include QMK_KEYBOARD_H

#include "gesture.h"

#define IDENTITY_COLS 8
#define GESTURE_ROW 31

static inline keypos_t identity_pos(uint16_t usage) {
    return (keypos_t){.row = (uint8_t)(usage / IDENTITY_COLS + 1), .col = (uint8_t)(usage % IDENTITY_COLS)};
}

static inline keypos_t gesture_pos(gesture_dir_t d) {
    return (keypos_t){.row = GESTURE_ROW, .col = (uint8_t)d};
}

/* An event on a gesture cell: only gesture taps (mouse.c) make those. */
static inline bool gesture_is_cell_event(const keyrecord_t *record) {
    return IS_KEYEVENT(record->event) && record->event.key.row == GESTURE_ROW;
}

static inline bool identity_is(keypos_t k, uint16_t usage) {
    keypos_t p = identity_pos(usage);
    return k.row == p.row && k.col == p.col;
}

_Static_assert(MATRIX_COLS == IDENTITY_COLS, "identity layout is 8 columns");
_Static_assert(MATRIX_ROWS == GESTURE_ROW + 1, "gesture cells are the last row");
_Static_assert(GESTURE_DIR_COUNT <= IDENTITY_COLS, "gesture cells fit one row");
/* Cells vial.json draws in the HUGE PLUS view. */
_Static_assert(KC_MS_BTN1 / 8 + 1 == 27 && KC_MS_BTN1 % 8 == 1, "Mouse1 is cell 27,1");
_Static_assert(KC_MS_WH_UP / 8 + 1 == 28 && KC_MS_WH_UP % 8 == 1, "WH_UP is cell 28,1");
_Static_assert(KC_MS_WH_DOWN / 8 + 1 == 28 && KC_MS_WH_DOWN % 8 == 2, "WH_DOWN is cell 28,2");
_Static_assert(KC_MS_WH_LEFT / 8 + 1 == 28 && KC_MS_WH_LEFT % 8 == 3, "WH_LEFT is cell 28,3");
_Static_assert(KC_MS_WH_RIGHT / 8 + 1 == 28 && KC_MS_WH_RIGHT % 8 == 4, "WH_RIGHT is cell 28,4");
_Static_assert(KC_MS_UP / 8 + 1 == 26 && KC_MS_UP % 8 == 5, "MS_UP is cell 26,5");
