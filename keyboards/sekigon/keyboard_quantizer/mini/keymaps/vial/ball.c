// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "dynamic_keymap.h"
#include "qmk_settings.h"

#include "ball.h"
#include "gesture.h"
#include "hid_input.h"
#include "identity.h"
#include "modes.h"
#include "mouse.h"
#include "pointer.h"
#include "scroll_out.h"
#include "settings.h"
#include "wheel.h"

#define SLOW_SCROLL_MUL 4
/* How long what the keymap holds for gestures is trusted before reading it
 * again (Vial can change it at any time; layers change it too, and those
 * are part of the key). */
#define KEYMAP_INFO_MS 250

typedef enum { BALL_CURSOR, BALL_SCROLL, BALL_GESTURE } ball_mode_t;

static pointer_t     pointer;
static gesture_t     gesture;
static ball_mode_t   last_mode = BALL_CURSOR;
/* Gesture taps that had to wait for room in QMK's tap-hold queue. */
static gesture_dir_t deferred_dir = GESTURE_NONE;
static uint8_t       deferred_taps;

/* ------------------------------------------------------------- keymap */

static bool is_action_keycode(uint16_t kc) {
    return kc != KC_NO && kc != KC_TRNS;
}

/* The key a gesture cell holds, resolved the way QMK resolves its tap:
 * through the active layers (a transparent cell falls through), layer 0
 * reading layer 1's cells (keymap.c). */
static uint16_t gesture_keycode(gesture_dir_t d) {
    keypos_t pos = gesture_pos(d);
    return keymap_key_to_keycode(layer_switch_get_layer(pos), pos);
}

typedef struct {
    bool         valid;
    layer_state_t layers; /* layer_state | default_layer_state it was read for */
    uint16_t     read_at;
    bool         by_layout; /* the top layer turns gestures on by itself */
    uint8_t      diagonals; /* gesture_cfg_t.diagonals */
} keymap_info_t;

static keymap_info_t info;

/* A layer (not layer 0) that maps gesture cells and leaves the four
 * ball-direction cells (26,5 26,6 26,7 27,0) empty uses gestures without a
 * key. The ball never fires those cells; mapping one only turns this off. */
static bool layer_uses_gestures(uint8_t layer) {
    if (layer == 0) {
        return false;
    }
    bool mapped = false;
    for (uint8_t d = 0; d < GESTURE_DIR_COUNT; d++) {
        keypos_t p = gesture_pos((gesture_dir_t)d);
        mapped |= is_action_keycode(dynamic_keymap_get_keycode(layer, p.row, p.col));
    }
    static const uint16_t ball_cells[] = {KC_MS_UP, KC_MS_DOWN, KC_MS_LEFT, KC_MS_RIGHT};
    for (uint8_t i = 0; i < sizeof(ball_cells) / sizeof(ball_cells[0]); i++) {
        keypos_t p = identity_pos(ball_cells[i]);
        if (is_action_keycode(dynamic_keymap_get_keycode(layer, p.row, p.col))) {
            return false;
        }
    }
    return mapped;
}

static const keymap_info_t *keymap_info(void) {
    layer_state_t layers = layer_state | default_layer_state;
    if (info.valid && info.layers == layers && timer_elapsed(info.read_at) < KEYMAP_INFO_MS) {
        return &info;
    }
    info.valid     = true;
    info.layers    = layers;
    info.read_at   = timer_read();
    info.by_layout = layer_uses_gestures(get_highest_layer(layers));
    info.diagonals = 0;
    /* Diagonals take part only where a key is mapped. */
    for (uint8_t d = GESTURE_UP_RIGHT; d < GESTURE_DIR_COUNT; d++) {
        if (is_action_keycode(gesture_keycode((gesture_dir_t)d))) {
            info.diagonals |= (uint8_t)(1u << (d - GESTURE_UP_RIGHT));
        }
    }
    return &info;
}

/* --------------------------------------------------------------- mode */

static ball_mode_t ball_mode(void) {
    if (modes_active(KQ_MODE_SCROLL)) {
        return BALL_SCROLL;
    }
    if (modes_active(KQ_MODE_GESTURE) || keymap_info()->by_layout) {
        return BALL_GESTURE;
    }
    return BALL_CURSOR;
}

/* A new ball mode must not inherit the last one's half-done movement: a
 * gesture stroke, waiting gesture taps, the cursor's recoil memory. */
static ball_mode_t sync_ball_mode(void) {
    ball_mode_t mode = ball_mode();
    if (mode != last_mode) {
        gesture_reset(&gesture);
        deferred_taps = 0;
        if (mode != BALL_CURSOR) {
            pointer_clear_recoil(&pointer);
        }
        last_mode = mode;
    }
    return mode;
}

char ball_mode_letter(void) {
    switch (ball_mode()) {
        case BALL_SCROLL:
            return 's';
        case BALL_GESTURE:
            return 'g';
        default:
            return 'c';
    }
}

/* ------------------------------------------------------------ gesture */

/* Taps the gesture cell itself, like a key: whatever is mapped there runs,
 * tap dances and LCTL(kc) included, and if QMK holds the tap back while a
 * tap-hold key decides, it still resolves to that cell's key later. */
static void tap_gesture_cell(gesture_dir_t d) {
    keypos_t pos = gesture_pos(d);
    mouse_inject_begin();
    action_exec((keyevent_t){.type = KEY_EVENT, .key = pos, .pressed = true, .time = timer_read() | 1});
    qs_wait_ms(QS_tap_code_delay);
    action_exec((keyevent_t){.type = KEY_EVENT, .key = pos, .pressed = false, .time = timer_read() | 1});
    mouse_inject_end();
}

/* Taps that waited for room in QMK's queue go first, in order. */
static void flush_deferred_gestures(void) {
    while (deferred_taps > 0 && mouse_can_tap()) {
        /* Counted down first: the key may be CLR, which clears the count. */
        deferred_taps--;
        tap_gesture_cell(deferred_dir);
    }
}

static void fire_gesture(gesture_dir_t d) {
    if (d == GESTURE_NONE || mouse_injecting() || !is_action_keycode(gesture_keycode(d))) {
        return;
    }
    /* A stroke keeps one direction; taps of an earlier one are stale. */
    if (deferred_dir != d) {
        deferred_taps = 0;
        deferred_dir  = d;
    }
    if (deferred_taps < GESTURE_BACKLOG_MAX) {
        deferred_taps++;
    }
    flush_deferred_gestures();
}

/* -------------------------------------------------------------- input */

static int32_t scroll_divisor(void) {
    int32_t div = settings_get(SETTING_SCROLL_DIV);
    return modes_slow_scroll_active() ? div * SLOW_SCROLL_MUL : div;
}

void kq_mouse_report(const hid_mouse_t *m) {
    wheel_physical(m->wheel, m->pan);
    if (m->x == 0 && m->y == 0) {
        return;
    }
    switch (sync_ball_mode()) {
        case BALL_SCROLL:
            /* Ball up scrolls up: v = -y (sensor y grows downwards). */
            scroll_out_ball(m->x, -(int32_t)m->y, scroll_divisor());
            mouse_request_send();
            break;
        case BALL_GESTURE: {
            gesture_cfg_t cfg = {settings_get(SETTING_GESTURE_STEP), keymap_info()->diagonals};
            fire_gesture(gesture_move(&gesture, &cfg, m->x, m->y, timer_read()));
            break;
        }
        default: {
            int32_t x, y;
            pointer_move(&pointer, m->x, m->y, settings_get(SETTING_CURSOR_SCALE), modes_active(KQ_MODE_SLOW), &x, &y);
            mouse_add_motion(x, y);
            break;
        }
    }
}

void kq_mouse_scan_end(void) {
    wheel_scan_end();
    if (sync_ball_mode() == BALL_GESTURE) {
        flush_deferred_gestures();
        fire_gesture(gesture_poll(&gesture, timer_read()));
    }
}

void kq_mouse_device_lost(void) {
    mouse_reset_latches();
}

/* ---------------------------------------------------------- lifecycle */

void ball_setting_changed(setting_t s) {
    if (s == SETTING_CURSOR_SCALE) {
        pointer_reset(&pointer);
    } else if (s == SETTING_GESTURE_STEP) {
        gesture_reset(&gesture);
    }
}

void ball_reset(void) {
    pointer_reset(&pointer);
    gesture_reset(&gesture);
    deferred_taps = 0;
    last_mode     = BALL_CURSOR;
    info.valid    = false;
}

void ball_init(void) {
    ball_reset();
}
