// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pointer.h"

#include <string.h>

/* A reverse step of at most RECOIL_MAX counts right after a step of at least
 * RECOIL_PREV is the sensor settling, not the hand. */
#define RECOIL_PREV 64
#define RECOIL_MAX 8

void pointer_reset(pointer_t *p) {
    memset(p, 0, sizeof(*p));
}

void pointer_clear_recoil(pointer_t *p) {
    p->prev_x = 0;
    p->prev_y = 0;
}

static int32_t abs32(int32_t v) {
    return v < 0 ? -v : v;
}

static int16_t filter_recoil(int16_t now, int16_t *prev) {
    if (now == 0) {
        *prev = 0;
        return 0;
    }
    int16_t out = now;
    if (abs32(*prev) >= RECOIL_PREV && abs32(now) <= RECOIL_MAX && (now > 0) != (*prev > 0)) {
        out = 0;
    }
    *prev = now;
    return out;
}

static int32_t scale_axis(int32_t counts, uint8_t scale, int32_t den, int32_t *rem) {
    int32_t num = counts * scale + *rem;
    int32_t out = num / den; /* toward zero: both directions behave the same */
    *rem        = num - out * den;
    return out;
}

void pointer_move(pointer_t *p, int16_t x, int16_t y, uint8_t scale, bool slow, int32_t *out_x, int32_t *out_y) {
    *out_x = 0;
    *out_y = 0;
    /* Wheel-only reports carry no motion: they must not reset the filter. */
    if (x == 0 && y == 0) {
        return;
    }
    int32_t den = POINTER_SCALE_UNIT * (slow ? POINTER_SLOW_DIV : 1);
    if (den != p->den) {
        p->den   = den;
        p->rem_x = 0;
        p->rem_y = 0;
    }
    x      = filter_recoil(x, &p->prev_x);
    y      = filter_recoil(y, &p->prev_y);
    *out_x = scale_axis(x, scale, den, &p->rem_x);
    *out_y = scale_axis(y, scale, den, &p->rem_y);
}
