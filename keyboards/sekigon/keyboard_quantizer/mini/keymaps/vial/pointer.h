// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Cursor motion: ball counts -> cursor counts.
 *
 * One fixed-point step per axis: counts x scale / 16 (x 1/4 in slow cursor),
 * with the remainder carried to the next report so slow movement is not
 * rounded away. Before that, a recoil filter drops the small reverse tick a
 * ball sensor reports right after a fast flick stops. Pure C, tested on the
 * host. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define POINTER_SCALE_UNIT 16 /* scale 16 = 1:1 */
#define POINTER_SLOW_DIV 4

typedef struct {
    int32_t rem_x;
    int32_t rem_y;
    int32_t den; /* denominator the remainders are in */
    int16_t prev_x;
    int16_t prev_y;
} pointer_t;

void pointer_reset(pointer_t *p);
/* Forget the last motion (the recoil filter's memory), e.g. when the ball
 * starts driving something else. */
void pointer_clear_recoil(pointer_t *p);
void pointer_move(pointer_t *p, int16_t x, int16_t y, uint8_t scale, bool slow, int32_t *out_x, int32_t *out_y);
