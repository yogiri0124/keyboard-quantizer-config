// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string.h>

#include "scroll_out.h"
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
#    include "usb_main.h"
#    define QUEUE_PER_DETENT (MOUSE_WHEEL_RESOLUTION_MULTIPLIER * 256)
#else
#    define QUEUE_PER_DETENT 256
#endif

enum { AXIS_V = 0, AXIS_H = 1 };

static int32_t queue[2];    /* waiting to be sent */
static int32_t urgent[2];   /* modifier held: sent first, on its own */
static int32_t ball_rem[2]; /* ball counts x QUEUE_PER_DETENT not yet a unit */

/* Host counts per detent: 1 until the host enables the multiplier. */
static int32_t multiplier(int axis) {
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
    return usb_mouse_wheel_multiplier(axis == AXIS_V);
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
    (now ? urgent : queue)[a] += (int32_t)detents * QUEUE_PER_DETENT;
}

static void ball_axis(int a, int32_t c, int32_t counts_per_detent) {
    int64_t total = (int64_t)c * QUEUE_PER_DETENT + ball_rem[a];
    int32_t q     = (int32_t)(total / counts_per_detent);
    ball_rem[a]   = (int32_t)(total - (int64_t)q * counts_per_detent);
    queue[a] += q;
}

void scroll_out_ball(int32_t h, int32_t v, int32_t counts_per_detent) {
    if (counts_per_detent < 1) {
        counts_per_detent = 1;
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

void scroll_out_flush(report_mouse_t *mouse) {
    if (mouse == NULL) {
        return;
    }
    int32_t *q = (counts(urgent, AXIS_V) != 0 || counts(urgent, AXIS_H) != 0) ? urgent : queue;
    int8_t   sent[2];
    for (int a = 0; a < 2; a++) {
        int32_t c = counts(q, a);
        c         = c > 127 ? 127 : (c < -127 ? -127 : c);
        q[a] -= (int32_t)((int64_t)c * QUEUE_PER_DETENT / multiplier(a));
        sent[a] = (int8_t)c;
    }
    mouse->v = sent[AXIS_V];
    mouse->h = sent[AXIS_H];
}

void scroll_out_reset(void) {
    memset(queue, 0, sizeof(queue));
    memset(urgent, 0, sizeof(urgent));
    memset(ball_rem, 0, sizeof(ball_rem));
}
