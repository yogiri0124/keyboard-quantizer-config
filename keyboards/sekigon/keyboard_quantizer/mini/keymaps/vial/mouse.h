// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* The mouse side of the keymap: what the ball does (cursor, scroll or
 * gesture, see modes.h), and the one report that goes to the PC.
 *
 * Ball and wheel input arrives from mini/hid_input.c (kq_mouse_report);
 * buttons arrive through the matrix as KC_MS_BTN keys, so they can be
 * remapped like any key. The report sent to the PC carries the mousekey
 * buttons plus the drag locks, the cursor motion collected since the last
 * report, and the scroll output (scroll_out.h). */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "report.h"

void mouse_request_send(void);
/* Sends a report right away with only this motion and the buttons the PC
 * already has (wheel.c: scrolling that must beat a modifier release). */
void mouse_send_direct(const report_mouse_t *motion);
uint8_t mouse_sent_buttons(void);

/* Our own key events (wheel cells, gesture taps) are not "the next key" a
 * oneshot waits for. */
void mouse_inject_begin(void);
void mouse_inject_end(void);
bool mouse_injecting(void);

void mouse_reset_latches(void);              /* CLR, unplug */
void mouse_reset_settings_and_latches(void); /* RST */

void mouse_init(void);
void mouse_housekeeping(void);

void mouse_print_settings_string(void); /* SHOW: typed as text */
void mouse_cli_print(void);
