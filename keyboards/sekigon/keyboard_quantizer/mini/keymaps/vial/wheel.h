// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Wheel and tilt: the physical wheel, WH_* keys, and mousekey reports.
 *
 * A physical wheel movement taps its Vial cell (28,1 up, 28,2 down, 28,3 /
 * 28,4 tilt) through QMK, so whatever is mapped there runs, with layers and
 * tap-hold behaving as for any key. When the cell holds a WH_* key, the
 * amount the wheel turned rides along with the tap: same direction scrolls
 * as the device sent it, the opposite direction of the same axis inverts it,
 * and the other axis scrolls that way instead.
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
/* A tap of a wheel cell made by wheel_scan_end, not a key. */
bool wheel_is_injected(const keyrecord_t *record);
/* WH_* keys and wheel cell taps. Returns false when it consumed the event. */
bool wheel_process_record(uint16_t keycode, keyrecord_t *record);
void wheel_task(void); /* WH_* key repeat */
void wheel_reset(void);
