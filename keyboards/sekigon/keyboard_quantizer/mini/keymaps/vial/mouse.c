// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include <string.h>

#include "pointing_device.h"
#ifdef MOUSEKEY_ENABLE
#    include "mousekey.h"
#endif
#include "vial.h"
#include "dynamic_keymap.h"
#include "qmk_settings.h"
#include "action_tapping.h"
#ifdef COMBO_ENABLE
#    include "process_combo.h"
#endif

#include "mouse.h"
#include "gesture.h"
#include "hid_input.h"
#include "identity.h"
#include "locks.h"
#include "modes.h"
#include "pointer.h"
#include "scroll_out.h"
#include "settings.h"
#include "wheel.h"

#define SLOW_SCROLL_MUL 4
/* How long a layer's gesture cells are trusted before reading them again
 * (Vial can change them at any time). */
#define LAYER_INFO_MS 250

typedef enum { BALL_CURSOR, BALL_SCROLL, BALL_GESTURE } ball_mode_t;

static bool        send_wanted;
static uint8_t     sent_buttons;
static int32_t     pending_x;
static int32_t     pending_y;
static pointer_t   pointer;
static gesture_t   gesture;
static ball_mode_t last_mode = BALL_CURSOR;
static uint8_t     inject_depth;
/* A gesture tap that had to wait for room in QMK's tap-hold queue. */
static gesture_dir_t deferred_dir = GESTURE_NONE;
static uint8_t       deferred_taps;

/* ---------------------------------------------------------------- send */

void mouse_request_send(void) {
    send_wanted = true;
}

uint8_t mouse_sent_buttons(void) {
    return sent_buttons;
}

static uint8_t current_buttons(void) {
    uint8_t b = locks_buttons();
#ifdef MOUSEKEY_ENABLE
    b |= mousekey_get_report().buttons;
#endif
    return b;
}

static mouse_xy_report_t take_axis(int32_t *pending) {
    int32_t v = *pending;
    if (v > XY_REPORT_MAX) {
        v = XY_REPORT_MAX;
    } else if (v < XY_REPORT_MIN) {
        v = XY_REPORT_MIN;
    }
    *pending -= v;
    return (mouse_xy_report_t)v;
}

/* Builds and sends the regular report if it says anything new. */
static bool send_report(void) {
    report_mouse_t r = {0};
    r.buttons        = current_buttons();
    r.x              = take_axis(&pending_x);
    r.y              = take_axis(&pending_y);
    scroll_out_take(&r.v, &r.h);
    send_wanted  = pending_x != 0 || pending_y != 0 || scroll_out_pending();
    bool changed = r.buttons != sent_buttons || r.x != 0 || r.y != 0 || r.v != 0 || r.h != 0;
    if (changed) {
        host_mouse_send(&r);
        sent_buttons = r.buttons;
    }
    return changed;
}

void mouse_send_direct(const report_mouse_t *motion) {
    report_mouse_t r = *motion;
    r.buttons        = sent_buttons;
    host_mouse_send(&r);
}

/* Replaces QMK's: this keyboard has no sensor to poll, the report is built
 * from what the connected mouse and the keys did. */
bool pointing_device_task(void) {
    if (!send_wanted) {
        return false;
    }
    return send_report();
}

/* A mouse button key (KC_MS_BTN1..8): QMK's mousekey has the new state;
 * send it now, with the motion so far, so a click lands where the cursor
 * is. Pressing a button also ends its drag lock. */
void pointing_device_keycode_handler(uint16_t keycode, bool pressed) {
    if (!IS_MOUSEKEY_BUTTON(keycode)) {
        return;
    }
    if (pressed) {
        locks_button_pressed(keycode);
    }
    send_report();
}

/* -------------------------------------------------------------- inject */

void mouse_inject_begin(void) {
    inject_depth++;
}

void mouse_inject_end(void) {
    if (inject_depth > 0) {
        inject_depth--;
    }
}

bool mouse_injecting(void) {
    return inject_depth != 0;
}

bool mouse_can_tap(void) {
    uint8_t need = 2; /* press and release */
#ifdef COMBO_ENABLE
    /* A pending combo replays the keys it holds through the same queue. */
    need += combo_buffered_events();
#endif
    return action_tapping_waiting_room() >= need;
}

/* ------------------------------------------------------- ball / layers */

static uint8_t active_layer(void) {
    return get_highest_layer(layer_state | default_layer_state);
}

static bool is_action_keycode(uint16_t kc) {
    return kc != KC_NO && kc != KC_TRNS;
}

static uint16_t cell_keycode(uint8_t layer, keypos_t k) {
    return dynamic_keymap_get_keycode(layer, k.row, k.col);
}

typedef struct {
    uint8_t  layer;
    uint16_t read_at;
    bool     valid;
    bool     mapped;    /* any gesture cell has a key */
    bool     ball_free; /* the four ball-direction cells are empty */
} layer_info_t;

static layer_info_t layer_info;

static const layer_info_t *gesture_cells(uint8_t layer) {
    if (layer_info.valid && layer_info.layer == layer && timer_elapsed(layer_info.read_at) < LAYER_INFO_MS) {
        return &layer_info;
    }
    layer_info.layer     = layer;
    layer_info.read_at   = timer_read();
    layer_info.valid     = true;
    layer_info.mapped    = false;
    for (uint8_t d = 0; d < GESTURE_DIR_COUNT; d++) {
        if (is_action_keycode(cell_keycode(layer, gesture_pos((gesture_dir_t)d)))) {
            layer_info.mapped = true;
        }
    }
    static const uint16_t ball_cells[] = {KC_MS_UP, KC_MS_DOWN, KC_MS_LEFT, KC_MS_RIGHT};
    layer_info.ball_free               = true;
    for (uint8_t i = 0; i < sizeof(ball_cells) / sizeof(ball_cells[0]); i++) {
        if (is_action_keycode(cell_keycode(layer, identity_pos(ball_cells[i])))) {
            layer_info.ball_free = false;
        }
    }
    return &layer_info;
}

/* A layer (not layer 0) that maps gesture cells and leaves the four
 * ball-direction cells (26,5 26,6 26,7 27,0) empty uses gestures without a
 * key. The ball never fires those cells; mapping one only turns this off. */
static bool gesture_by_layout(void) {
    uint8_t layer = active_layer();
    if (layer == 0) {
        return false;
    }
    const layer_info_t *li = gesture_cells(layer);
    return li->mapped && li->ball_free;
}

static ball_mode_t ball_mode(void) {
    if (modes_active(KQ_MODE_SCROLL)) {
        return BALL_SCROLL;
    }
    if (modes_active(KQ_MODE_GESTURE) || gesture_by_layout()) {
        return BALL_GESTURE;
    }
    return BALL_CURSOR;
}

/* A new ball mode must not inherit the last one's half-done movement: a
 * gesture stroke or the cursor's recoil memory. */
static ball_mode_t sync_ball_mode(void) {
    ball_mode_t mode = ball_mode();
    if (mode != last_mode) {
        gesture_reset(&gesture);
        if (mode != BALL_CURSOR) {
            pointer_clear_recoil(&pointer);
        }
        last_mode = mode;
    }
    return mode;
}

/* The key a gesture cell holds, resolved the way QMK will resolve its tap:
 * through the active layers (a transparent cell falls through), layer 0
 * reading layer 1's cells (keymap.c). */
static uint16_t gesture_keycode(gesture_dir_t d) {
    keypos_t pos = gesture_pos(d);
    return keymap_key_to_keycode(layer_switch_get_layer(pos), pos);
}

/* Diagonals take part only where a key is mapped. */
static uint8_t mapped_diagonals(void) {
    uint8_t mask = 0;
    for (uint8_t d = GESTURE_UP_RIGHT; d < GESTURE_DIR_COUNT; d++) {
        if (is_action_keycode(gesture_keycode((gesture_dir_t)d))) {
            mask |= (uint8_t)(1u << (d - GESTURE_UP_RIGHT));
        }
    }
    return mask;
}

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

/* Taps that waited for room in QMK's queue go first, in order. A different
 * direction cannot follow waiting taps (a stroke keeps one direction; a new
 * stroke drops them). */
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
    if (deferred_dir != d) {
        deferred_taps = 0;
        deferred_dir  = d;
    }
    if (deferred_taps < GESTURE_BACKLOG_MAX) {
        deferred_taps++;
    }
    flush_deferred_gestures();
}

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
            send_wanted = true;
            break;
        case BALL_GESTURE: {
            gesture_cfg_t cfg = {settings_get(SETTING_GESTURE_STEP), mapped_diagonals()};
            fire_gesture(gesture_move(&gesture, &cfg, m->x, m->y, timer_read()));
            break;
        }
        default: {
            int32_t x, y;
            pointer_move(&pointer, m->x, m->y, settings_get(SETTING_CURSOR_SCALE), modes_active(KQ_MODE_SLOW), &x, &y);
            if (x != 0 || y != 0) {
                pending_x += x;
                pending_y += y;
                send_wanted = true;
            }
            break;
        }
    }
}

void kq_mouse_scan_end(void) {
    wheel_scan_end();
    if (sync_ball_mode() == BALL_GESTURE) {
        flush_deferred_gestures();
        fire_gesture(gesture_poll(&gesture, timer_read()));
    } else {
        deferred_taps = 0;
    }
}

void kq_mouse_device_lost(void) {
    mouse_reset_latches();
}

void mouse_setting_changed(setting_t s) {
    if (s == SETTING_CURSOR_SCALE) {
        pointer_reset(&pointer);
    } else if (s == SETTING_GESTURE_STEP) {
        gesture_reset(&gesture);
    }
}

/* ---------------------------------------------------------- lifecycle */

void mouse_reset_latches(void) {
    modes_reset();
    locks_clear();
    wheel_reset();
    scroll_out_reset();
    pointer_reset(&pointer);
    gesture_reset(&gesture);
    deferred_taps = 0;
    pending_x     = 0;
    pending_y     = 0;
    last_mode     = BALL_CURSOR;
    send_wanted   = true; /* drag-lock buttons are gone */
}

void mouse_reset_settings_and_latches(void) {
    mouse_reset_latches();
    settings_reset();
}

void mouse_init(void) {
    pointer_reset(&pointer);
    gesture_reset(&gesture);
}

void mouse_housekeeping(void) {
    modes_new_pass();
    locks_task();
    settings_task();
    wheel_task();
    /* The PC may switch the wheel multiplier at any time, which can make a
     * waiting fraction sendable. */
    if (scroll_out_pending()) {
        send_wanted = true;
    }
}

/* ---------------------------------------------------------- reporting */

static char mode_letter(void) {
    switch (ball_mode()) {
        case BALL_SCROLL:
            return 's';
        case BALL_GESTURE:
            return 'g';
        default:
            return 'c';
    }
}

static char *append_uint(char *p, unsigned v) {
    char tmp[4];
    int  n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0 && n < 4);
    while (n > 0) {
        *p++ = tmp[--n];
    }
    return p;
}

/* "c s32 t50 d16": mode (cursor / scroll / gesture), then the three
 * values. */
void mouse_print_settings_string(void) {
    char  buf[24];
    char *p = buf;
    *p++    = mode_letter();
    *p++    = ' ';
    *p++    = 's';
    p       = append_uint(p, settings_get(SETTING_SCROLL_DIV));
    *p++    = ' ';
    *p++    = 't';
    p       = append_uint(p, settings_get(SETTING_GESTURE_STEP));
    *p++    = ' ';
    *p++    = 'd';
    p       = append_uint(p, settings_get(SETTING_CURSOR_SCALE));
    *p++    = '\n';
    *p      = '\0';
    send_string(buf);
}

void mouse_cli_print(void) {
    printf("mode %c  s%u t%u d%u  slow %u  lock L%u M%u R%u  mod C%u S%u A%u W%u\n", mode_letter(),
           (unsigned)settings_get(SETTING_SCROLL_DIV), (unsigned)settings_get(SETTING_GESTURE_STEP),
           (unsigned)settings_get(SETTING_CURSOR_SCALE), (unsigned)modes_active(KQ_MODE_SLOW), (unsigned)locks_drag(0),
           (unsigned)locks_drag(1), (unsigned)locks_drag(2), (unsigned)locks_mod(0), (unsigned)locks_mod(1),
           (unsigned)locks_mod(2), (unsigned)locks_mod(3));
}
