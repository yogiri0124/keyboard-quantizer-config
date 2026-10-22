// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gesture.h"

#include <string.h>

/* Angles in 1/4096 of a turn, counterclockwise from right (screen up is
 * positive, so sensor y is negated). */
#define TURN 4096
#define HALF_TURN 2048
#define QUARTER_TURN 1024
/* How much nearer the winning direction must be than the runner-up while
 * the movement is still short: about 5.6 degrees either side of a border. */
#define BORDER_MARGIN 128
/* 1/sqrt(2) x 256, for steps along a diagonal. */
#define DIAG_256 181

static const int16_t centre[GESTURE_DIR_COUNT] = {
    [GESTURE_RIGHT] = 0,         [GESTURE_LEFT] = HALF_TURN,
    [GESTURE_UP] = QUARTER_TURN, [GESTURE_DOWN] = 3 * QUARTER_TURN,
    [GESTURE_UP_RIGHT] = 512,    [GESTURE_UP_LEFT] = 1536,
    [GESTURE_DOWN_RIGHT] = 3584, [GESTURE_DOWN_LEFT] = 2560,
};

static int32_t abs32(int32_t v) {
    return v < 0 ? -v : v;
}

/* atan(num / den) for 0 <= num <= den, in 1/4096 turn (0..512). Uses
 * atan(t) ~ pi/4 t + 0.273 t (1 - t), within about 0.3 degree. */
static int32_t octant_angle(int32_t num, int32_t den) {
    int64_t t = ((int64_t)num << 12) / den; /* Q12, 0..4096 */
    return (int32_t)((512 * t) >> 12) + (int32_t)((178 * t * (4096 - t)) >> 24);
}

static int32_t angle_of(int32_t x, int32_t y_up) {
    int32_t ax = abs32(x), ay = abs32(y_up);
    int32_t a = ax >= ay ? octant_angle(ay, ax) : QUARTER_TURN - octant_angle(ax, ay);
    if (x >= 0 && y_up >= 0) {
        return a;
    }
    if (x < 0 && y_up >= 0) {
        return HALF_TURN - a;
    }
    if (x < 0) {
        return HALF_TURN + a;
    }
    return (TURN - a) & (TURN - 1);
}

static int32_t angle_distance(int32_t a, int32_t b) {
    int32_t d = abs32(a - b) & (TURN - 1);
    return d > HALF_TURN ? TURN - d : d;
}

static bool usable(gesture_dir_t d, uint8_t diagonals) {
    return d < GESTURE_UP_RIGHT || (diagonals >> (d - GESTURE_UP_RIGHT)) & 1;
}

gesture_dir_t gesture_classify(int32_t x, int32_t y, uint8_t diagonals, bool margin) {
    if (x == 0 && y == 0) {
        return GESTURE_NONE;
    }
    int32_t       a      = angle_of(x, -y);
    gesture_dir_t best   = GESTURE_NONE;
    int32_t       d_best = TURN, d_next = TURN;
    for (int d = 0; d < GESTURE_DIR_COUNT; d++) {
        if (!usable((gesture_dir_t)d, diagonals)) {
            continue;
        }
        int32_t dist = angle_distance(a, centre[d]);
        if (dist < d_best) {
            d_next = d_best;
            d_best = dist;
            best   = (gesture_dir_t)d;
        } else if (dist < d_next) {
            d_next = dist;
        }
    }
    if (margin && d_next - d_best < BORDER_MARGIN) {
        return GESTURE_NONE;
    }
    return best;
}

/* Movement along d, x256. */
static int32_t along(gesture_dir_t d, int32_t dx, int32_t dy) {
    switch (d) {
        case GESTURE_RIGHT:
            return dx * 256;
        case GESTURE_LEFT:
            return -dx * 256;
        case GESTURE_UP:
            return -dy * 256;
        case GESTURE_DOWN:
            return dy * 256;
        case GESTURE_UP_RIGHT:
            return (dx - dy) * DIAG_256;
        case GESTURE_UP_LEFT:
            return (-dx - dy) * DIAG_256;
        case GESTURE_DOWN_RIGHT:
            return (dx + dy) * DIAG_256;
        case GESTURE_DOWN_LEFT:
            return (-dx + dy) * DIAG_256;
        case GESTURE_NONE:
            break;
    }
    return 0;
}

/* Length of (x, y) to within 7%: max + 3/8 min. */
static int32_t length_of(int32_t x, int32_t y) {
    int32_t ax = abs32(x), ay = abs32(y);
    int32_t hi = ax > ay ? ax : ay, lo = ax > ay ? ay : ax;
    return hi + (lo * 3) / 8;
}

void gesture_reset(gesture_t *g) {
    memset(g, 0, sizeof(*g));
    g->dir = GESTURE_NONE;
}

static void end_stroke(gesture_t *g) {
    g->active   = false;
    g->dir      = GESTURE_NONE;
    g->sum_x    = 0;
    g->sum_y    = 0;
    g->progress = 0;
    g->backlog  = 0;
}

static gesture_dir_t try_tap(gesture_t *g, uint16_t now) {
    if (g->backlog == 0) {
        return GESTURE_NONE;
    }
    if (g->tapped && (uint16_t)(now - g->last_tap) < GESTURE_MIN_INTERVAL_MS) {
        return GESTURE_NONE;
    }
    g->backlog--;
    g->tapped   = true;
    g->last_tap = now;
    return g->dir;
}

/* Turns whole steps of progress into waiting taps. */
static void take_steps(gesture_t *g, int32_t step256) {
    while (g->progress >= step256) {
        g->progress -= step256;
        if (g->backlog < GESTURE_BACKLOG_MAX) {
            g->backlog++;
        } else {
            g->progress %= step256; /* more than can be tapped: drop it */
            break;
        }
    }
}

gesture_dir_t gesture_move(gesture_t *g, const gesture_cfg_t *cfg, int16_t dx, int16_t dy, uint16_t now) {
    if (dx == 0 && dy == 0) {
        return gesture_poll(g, now);
    }
    int32_t step    = cfg->step > 0 ? cfg->step : 1;
    int32_t step256 = step * 256;
    g->active       = true;
    g->last_motion  = now;
    if (g->dir == GESTURE_NONE) {
        g->sum_x += dx;
        g->sum_y += dy;
        int32_t len = length_of(g->sum_x, g->sum_y);
        if (len >= step) {
            /* Near a border, wait for a clearer heading, but not past two
             * steps: then the nearest direction wins. */
            gesture_dir_t d = gesture_classify(g->sum_x, g->sum_y, cfg->diagonals, len < 2 * step);
            if (d != GESTURE_NONE) {
                g->dir      = d;
                g->progress = along(d, g->sum_x, g->sum_y) - step256;
                if (g->progress < 0) {
                    g->progress = 0;
                }
                g->backlog = 1;
                take_steps(g, step256);
            }
        }
    } else {
        g->progress += along(g->dir, dx, dy);
        if (g->progress < 0) {
            g->progress = 0;
        }
        take_steps(g, step256);
    }
    return try_tap(g, now);
}

gesture_dir_t gesture_poll(gesture_t *g, uint16_t now) {
    if (!g->active) {
        return GESTURE_NONE;
    }
    gesture_dir_t d = try_tap(g, now);
    if (d != GESTURE_NONE) {
        return d;
    }
    if (g->backlog == 0 && (uint16_t)(now - g->last_motion) >= GESTURE_IDLE_MS) {
        end_stroke(g);
    }
    return GESTURE_NONE;
}
