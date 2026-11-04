// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Scroll output queue. Every wheel and ball-scroll amount goes through here
 * and is sent as high-resolution wheel counts (patches/0002): exact, with
 * nothing added or smoothed, so the host and the app see exactly what the
 * hand did, as finely as the host allows.
 *
 * Amounts are kept as integers in 1/QUEUE_PER_DETENT of a detent, which the
 * host multiplier (1 or MOUSE_WHEEL_RESOLUTION_MULTIPLIER) divides exactly.
 *
 * Paced: ordinary scrolling goes out at most once per SCROLL_OUT_INTERVAL_MS,
 * everything gathered since in one report. Each report is one wheel message
 * in the PC's input queue, and an app that scrolls slower than they arrive
 * lets them pile up until Windows drops input and beeps; a mouse reporting
 * every millisecond, or a backlog sent out back to back, did exactly that.
 * The interval is about one frame of a 360 Hz display, so the picture gets
 * as many steps as before. Scrolling after a pause goes out at once. With
 * 16-bit wheel fields (WHEEL_EXTENDED_REPORT, patches/0006) a report holds
 * any amount a hand can make in one interval, so nothing waits longer than
 * that; with 8-bit ones a report holds +-127 counts and the rest waits.
 * Pure C, tested on the host. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* One report's wheel field (mouse_hv_report_t). */
#ifdef WHEEL_EXTENDED_REPORT
typedef int16_t scroll_count_t;
#    define SCROLL_OUT_REPORT_MAX 32767
#else
typedef int8_t scroll_count_t;
#    define SCROLL_OUT_REPORT_MAX 127
#endif

#ifndef SCROLL_OUT_INTERVAL_MS
#    define SCROLL_OUT_INTERVAL_MS 3
#endif

/* Whole detents from a wheel, a WH_* key or a macro. now: urgent, sent
 * before anything else, unpaced, and never netted against other scrolling
 * (a modifier is down, so it must reach the host while it still is). */
void scroll_out_detents(int16_t detents, bool vertical, bool now);
/* Ball scroll in raw ball counts (h right, v up); counts_per_detent of them
 * make one detent. The division remainder carries over, so nothing is lost. */
void scroll_out_ball(int32_t h, int32_t v, int32_t counts_per_detent);

/* At least one host count is waiting (maybe for its turn: see
 * scroll_out_take). */
bool scroll_out_pending(void);
/* Moves what is due at time_ms (timer_read()) out for the report about to be
 * sent, up to one report's worth per axis: urgent output first, on its own;
 * otherwise ordinary scrolling, when SCROLL_OUT_INTERVAL_MS have passed since
 * the last report that carried some. Zero when nothing is due. */
void scroll_out_take(uint16_t time_ms, scroll_count_t *v, scroll_count_t *h);
/* Urgent output only (modifier held), for sending it before the modifier is
 * released without taking any ordinary scrolling along. */
bool scroll_out_urgent_pending(void);
void scroll_out_take_urgent(scroll_count_t *v, scroll_count_t *h);
/* Drops everything waiting (RST / CLR / unplug). */
void scroll_out_reset(void);

/* Host counts per detent the PC asked for: 1 until it enables the
 * multiplier. Provided by the firmware (wheel.c) or the test. */
int32_t scroll_out_host_multiplier(bool vertical);
