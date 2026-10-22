// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* The mouse report that goes to the PC: the mousekey buttons plus the drag
 * locks, the cursor motion collected since the last report (ball.c), and the
 * scroll output (scroll_out.h). Buttons of the connected mouse arrive
 * through the matrix as KC_MS_BTN keys, so they can be remapped like any
 * key. Also the bookkeeping for our own key taps, and resets. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "action.h"
#include "report.h"

void mouse_request_send(void);
void mouse_add_motion(int32_t x, int32_t y); /* cursor counts */
/* Sends a report right away with only this motion and the buttons the PC
 * already has (wheel.c: scrolling that must beat a modifier release). */
void mouse_send_direct(const report_mouse_t *motion);

/* Our own key taps (wheel cells, gesture cells): a press and, hold_ms
 * later, a release of the cell at pos, run through QMK like a key. They are
 * not "the next key" a oneshot waits for. */
void mouse_tap_cell(keypos_t pos, uint16_t hold_ms);
bool mouse_injecting(void);
/* Whether one more injected tap (press and release) fits in QMK's queues
 * right now (patches/0004): while a tap-hold key is undecided, QMK queues
 * events, and overflowing that queue would clear every key. */
bool mouse_can_tap(void);

void mouse_reset_latches(void);              /* CLR, unplug */
void mouse_reset_settings_and_latches(void); /* RST */

void mouse_init(void);
void mouse_housekeeping(void);

void mouse_print_settings_string(void); /* SHOW: typed as text */
void mouse_cli_print(void);
