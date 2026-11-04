// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string.h>

#include "scroll_out.h"
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
#    define QUEUE_PER_DETENT (MOUSE_WHEEL_RESOLUTION_MULTIPLIER * 256)
#else
#    define QUEUE_PER_DETENT 256
#endif

enum { AXIS_V = 0, AXIS_H = 1 };

static int32_t queue[2];     /* waiting to be sent */
static int32_t urgent[2];    /* modifier held: sent first, on its own */
static int32_t ball_rem[2];  /* ball counts x QUEUE_PER_DETENT not yet a unit, */
static int32_t ball_div = 1; /* ... as a remainder of this divisor */
static bool     paced;        /* an ordinary report went out at ... */
static uint16_t paced_at;     /* ... this time */

/* Far more than a hand can scroll before the next report; it only keeps
 * absurd input from wrapping the sum around. */
#define QUEUE_LIMIT (INT32_MAX / 2)

static void add(int32_t *q, int64_t amount) {
    int64_t v = (int64_t)*q + amount;
    *q        = (int32_t)(v > QUEUE_LIMIT ? QUEUE_LIMIT : (v < -QUEUE_LIMIT ? -QUEUE_LIMIT : v));
}

static int32_t multiplier(int axis) {
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
    int32_t m = scroll_out_host_multiplier(axis == AXIS_V);
    return m < 1 ? 1 : m;
#else
    (void)axis;
    return 1;
#endif
}

/* Whole host counts in q for an axis, toward zero. Exact: QUEUE_PER_DETENT
 * is a multiple of every multiplier the host can pick. */
static int32_t counts(const int32_t *q, int axis) {
    return (int32_t)((int64_t)q[axis] * multiplier(axis) / QUEUE_PER_DETENT);
}

void scroll_out_detents(int16_t detents, bool vertical, bool now) {
    int a = vertical ? AXIS_V : AXIS_H;
    add(&(now ? urgent : queue)[a], (int64_t)detents * QUEUE_PER_DETENT);
}

static void ball_axis(int a, int32_t c, int32_t counts_per_detent) {
    int64_t total = (int64_t)c * QUEUE_PER_DETENT + ball_rem[a];
    int64_t q     = total / counts_per_detent;
    ball_rem[a]   = (int32_t)(total - q * counts_per_detent);
    add(&queue[a], q);
}

void scroll_out_ball(int32_t h, int32_t v, int32_t counts_per_detent) {
    if (counts_per_detent < 1) {
        counts_per_detent = 1;
    }
    if (counts_per_detent != ball_div) {
        /* S.SCR or SCR+/SCR- changed the divisor: keep the carried fraction
         * the same size by rescaling it to the new one (nearest). */
        for (int a = 0; a < 2; a++) {
            int64_t r   = (int64_t)ball_rem[a] * counts_per_detent;
            ball_rem[a] = (int32_t)((r + (r >= 0 ? ball_div / 2 : -(ball_div / 2))) / ball_div);
        }
        ball_div = counts_per_detent;
    }
    ball_axis(AXIS_V, v, counts_per_detent);
    ball_axis(AXIS_H, h, counts_per_detent);
}

bool scroll_out_pending(void) {
    for (int a = 0; a < 2; a++) {
        if (counts(urgent, a) != 0 || counts(queue, a) != 0) {
            return true;
        }
    }
    return false;
}

bool scroll_out_urgent_pending(void) {
    return counts(urgent, AXIS_V) != 0 || counts(urgent, AXIS_H) != 0;
}

/* Moves up to one report's worth per axis; true if any count moved. */
static bool take_from(int32_t *q, scroll_count_t *v, scroll_count_t *h) {
    scroll_count_t sent[2];
    for (int a = 0; a < 2; a++) {
        /* One reading of the multiplier for both steps: the host can change
         * it from the USB interrupt at any time. */
        int32_t m = multiplier(a);
        int32_t c = (int32_t)((int64_t)q[a] * m / QUEUE_PER_DETENT);
        c         = c > SCROLL_OUT_REPORT_MAX ? SCROLL_OUT_REPORT_MAX : (c < -SCROLL_OUT_REPORT_MAX ? -SCROLL_OUT_REPORT_MAX : c);
        q[a] -= (int32_t)((int64_t)c * QUEUE_PER_DETENT / m);
        sent[a] = (scroll_count_t)c;
    }
    *v = sent[AXIS_V];
    *h = sent[AXIS_H];
    return sent[AXIS_V] != 0 || sent[AXIS_H] != 0;
}

void scroll_out_take(uint16_t time_ms, scroll_count_t *v, scroll_count_t *h) {
    if (scroll_out_urgent_pending()) {
        take_from(urgent, v, h);
        return;
    }
    /* uint16_t wrap: after a pause of a multiple of 65.536 s the first
     * report can wait up to one interval more. */
    if (paced && (uint16_t)(time_ms - paced_at) < SCROLL_OUT_INTERVAL_MS) {
        *v = *h = 0;
        return;
    }
    if (take_from(queue, v, h)) {
        paced    = true;
        paced_at = time_ms;
    }
}

void scroll_out_take_urgent(scroll_count_t *v, scroll_count_t *h) {
    take_from(urgent, v, h);
}

void scroll_out_reset(void) {
    memset(queue, 0, sizeof(queue));
    memset(urgent, 0, sizeof(urgent));
    memset(ball_rem, 0, sizeof(ball_rem));
    paced = false;
}
