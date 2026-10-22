// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Where the ball goes: cursor, scroll or gesture (modes.h decides which).
 * Ball and wheel input arrives here from mini/hid_input.c (kq_mouse_report,
 * kq_mouse_scan_end). */

#pragma once

#include <stdbool.h>
#include <stdint.h>

void ball_init(void);
/* Drops half-done movement: a gesture stroke, the cursor's remainders. */
void ball_reset(void);
/* 'c' cursor, 's' scroll, 'g' gesture. */
char ball_mode_letter(void);
