// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Wheel and tilt: the physical wheel, WH_* keys, and mousekey reports.
 *
 * Each detent of the physical wheel taps its Vial cell (28,1 up, 28,2 down,
 * 28,3 / 28,4 tilt) through QMK, like an encoder, so whatever is mapped there
 * runs, with layers and tap-hold behaving as for any key. A WH_* key in the
 * cell scrolls one detent per tap: the cell's own key scrolls as the device
 * sent it, the opposite key of the same axis inverts it, a key of the other
 * axis scrolls that way instead.
 *
 * Everything that scrolls ends in scroll_out.h. With a modifier down
 * (Ctrl + wheel = zoom) the scroll is sent at once, before the modifier can
 * be released. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "action.h"

void wheel_physical(int16_t wheel, int16_t pan); /* from a device report */
void wheel_scan_end(void);                       /* taps the cells for what came this scan */
/* An event on a wheel cell: only wheel_scan_end makes those. */
bool wheel_is_cell_event(const keyrecord_t *record);
/* WH_* keys and wheel cell taps. Returns false when it consumed the event. */
bool wheel_process_record(uint16_t keycode, keyrecord_t *record);
void wheel_task(void); /* WH_* key repeat */
void wheel_reset(void);
