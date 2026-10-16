// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Ball gestures: the ball, moved in one of eight directions, taps the key
 * mapped to that direction, again and again as it keeps moving.
 *
 * A stroke is one continuous movement, ended by the ball resting for
 * GESTURE_IDLE_MS. Its direction is decided once, when the ball has moved
 * one step (the threshold) from where it started, and then held for the rest
 * of the stroke, so a wobble cannot switch to a neighbour. A movement close
 * to the border between two directions waits a little longer (up to two
 * steps) for a clearer heading.
 *
 * Diagonals only exist where a key is mapped: with no key there, a diagonal
 * movement counts as the nearest of up / down / left / right, as with four
 * directions.
 *
 * After the first tap, every further step along the direction taps again: a
 * slow movement taps slowly, a fast flick taps more. Taps come at least
 * GESTURE_MIN_INTERVAL_MS apart, and at most GESTURE_BACKLOG_MAX wait, so a
 * hard flick does not keep tapping long after the ball stopped. Moving back
 * against the direction does nothing.
 *
 * Pure C, tested on the host. Timestamps are milliseconds (wrapping uint16). */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GESTURE_DIR_COUNT 8
#define GESTURE_IDLE_MS 120
#define GESTURE_MIN_INTERVAL_MS 40
#define GESTURE_BACKLOG_MAX 3

/* Order of the Vial cells (row 31, columns 0..7). */
typedef enum {
    GESTURE_RIGHT = 0,
    GESTURE_LEFT,
    GESTURE_UP,
    GESTURE_DOWN,
    GESTURE_UP_RIGHT,
    GESTURE_UP_LEFT,
    GESTURE_DOWN_RIGHT,
    GESTURE_DOWN_LEFT,
    GESTURE_NONE = -1,
} gesture_dir_t;

typedef struct {
    uint16_t step;      /* ball counts per tap, also to decide the direction */
    uint8_t  diagonals; /* bit (d - GESTURE_UP_RIGHT) set: diagonal d has a key */
} gesture_cfg_t;

typedef struct {
    bool          active; /* a stroke is under way */
    gesture_dir_t dir;    /* GESTURE_NONE until decided */
    int32_t       sum_x;  /* movement before the direction is decided */
    int32_t       sum_y;
    int32_t       progress; /* along dir since the last step, x256 */
    uint8_t       backlog;  /* taps waiting for the interval */
    uint16_t      last_motion;
    uint16_t      last_tap;
    bool          tapped; /* last_tap is valid */
} gesture_t;

void gesture_reset(gesture_t *g);
/* Ball movement (sensor axes: x right, y down). Returns the direction to tap
 * now, or GESTURE_NONE. */
gesture_dir_t gesture_move(gesture_t *g, const gesture_cfg_t *cfg, int16_t dx, int16_t dy, uint16_t now);
/* Call regularly: taps that waited for the interval, and the end of a
 * stroke. */
gesture_dir_t gesture_poll(gesture_t *g, uint16_t now);

/* Exposed for tests: direction of a movement, GESTURE_NONE if it is too
 * close to a border (margin applies). */
gesture_dir_t gesture_classify(int32_t x, int32_t y, uint8_t diagonals, bool margin);
