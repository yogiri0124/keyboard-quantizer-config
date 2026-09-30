// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Scroll output queue. Every wheel and ball-scroll amount goes through here
 * and is sent as high-resolution wheel counts (patches/0002): exact, with
 * nothing added or smoothed, so the host and the app see exactly what the
 * hand did, as finely as the host allows.
 *
 * Amounts are kept as integers in 1/QUEUE_PER_DETENT of a detent, which the
 * host multiplier (1 or MOUSE_WHEEL_RESOLUTION_MULTIPLIER) divides exactly;
 * a report carries at most ±127 counts and the rest waits for the next one. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "report.h"

/* Whole detents from a wheel, a WH_* key or a macro. now: urgent, sent
 * before anything else and never netted against other scrolling (a modifier
 * is down, so it must reach the host while it still is). */
void scroll_out_detents(int16_t detents, bool vertical, bool now);
/* Ball scroll in raw ball counts (h right, v up); counts_per_detent of them
 * make one detent. The division remainder carries over, so nothing is lost. */
void scroll_out_ball(int32_t h, int32_t v, int32_t counts_per_detent);

/* Host counts per detent: 1 until the host enables the multiplier. */
int32_t scroll_out_multiplier(bool vertical);
/* At least one host count is waiting. */
bool scroll_out_pending(void);
/* Moves up to ±127 counts per axis into the report about to be sent; urgent
 * output first, on its own. */
void scroll_out_flush(report_mouse_t *mouse);
/* Urgent output only (modifier held), for sending it before the modifier is
 * released without taking any ordinary scrolling along. */
bool scroll_out_urgent_pending(void);
void scroll_out_flush_urgent(report_mouse_t *mouse);
/* Drops everything waiting (RST / CLR / unplug). */
void scroll_out_reset(void);
