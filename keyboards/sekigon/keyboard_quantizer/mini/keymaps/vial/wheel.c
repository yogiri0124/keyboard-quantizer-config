// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#ifdef MOUSEKEY_ENABLE
#    include "mousekey.h"
#endif
#include "usb_main.h" /* mouse endpoint state: this board is ChibiOS only */

#include "wheel.h"
#include "identity.h"
#include "mouse.h"
#include "scroll_out.h"

#define CELLS 4 /* WH_UP, WH_DOWN, WH_LEFT, WH_RIGHT */
#define TAPS_PER_SCAN 8
#define URGENT_MAX_REPORTS 512
#define ENDPOINT_WAIT_MS 10

_Static_assert(KC_MS_WH_DOWN == KC_MS_WH_UP + 1 && KC_MS_WH_LEFT == KC_MS_WH_UP + 2 && KC_MS_WH_RIGHT == KC_MS_WH_UP + 3,
               "wheel keycodes are consecutive");

/* Wheel detents still to be tapped, as the device sent them. */
static int32_t backlog_wheel;
static int32_t backlog_pan;

static uint16_t repeat_kc = KC_NO;
static uint16_t repeat_since;
static bool     repeat_started;

static bool kc_vertical(uint16_t kc) {
    return kc == KC_MS_WH_UP || kc == KC_MS_WH_DOWN;
}

/* QMK and HID: WH_UP and WH_RIGHT are the positive directions. */
static bool kc_positive(uint16_t kc) {
    return kc == KC_MS_WH_UP || kc == KC_MS_WH_RIGHT;
}

/* Which cell a physical movement taps. A device matter, not the HID rule:
 * the ELECOM HUGE PLUS reports a tilt to the left as positive AC Pan
 * (measured with the "hid" CLI command), so the horizontal case is mirrored
 * to fire the cell on the side it was tilted towards. The scroll itself
 * passes the device value through unchanged. */
static uint16_t cell_kc_for(int32_t delta, bool vertical) {
    if (vertical) {
        return delta > 0 ? KC_MS_WH_UP : KC_MS_WH_DOWN;
    }
    return delta > 0 ? KC_MS_WH_LEFT : KC_MS_WH_RIGHT;
}

/* The device value one tap of a cell stands for (the inverse of
 * cell_kc_for): one detent, signed as the device sent it. */
static int16_t cell_detent(uint16_t cell_kc) {
    return (cell_kc == KC_MS_WH_UP || cell_kc == KC_MS_WH_LEFT) ? 1 : -1;
}

/* The wheel cell at k, or KC_NO. */
static uint16_t cell_at(keypos_t k) {
    for (uint16_t kc = KC_MS_WH_UP; kc <= KC_MS_WH_RIGHT; kc++) {
        if (identity_is(k, kc)) {
            return kc;
        }
    }
    return KC_NO;
}

int32_t scroll_out_host_multiplier(bool vertical) {
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
    return usb_mouse_wheel_multiplier(vertical);
#else
    (void)vertical;
    return 1;
#endif
}

static void wait_mouse_endpoint_idle(void) {
    extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];
    uint16_t                 start = timer_read();
    while (!usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_MOUSE]) &&
           timer_elapsed(start) < ENDPOINT_WAIT_MS) {
    }
}

/* Sends the urgent scroll (it came with a modifier held) right now, report
 * after report, then waits until the mouse endpoint has handed it to the PC,
 * so a modifier release queued next on the keyboard endpoint cannot overtake
 * it. URGENT_MAX_REPORTS only bounds how long this may block; anything left
 * goes with the regular send. Wheel only: cursor motion and ordinary
 * scrolling stay queued, and the buttons are the ones the PC already has.
 * sent: the caller already put scrolling on the wire. */
static void send_urgent(bool sent) {
    for (uint16_t i = 0; i < URGENT_MAX_REPORTS && scroll_out_urgent_pending(); i++) {
        report_mouse_t r = {0};
        scroll_out_take_urgent(&r.v, &r.h);
        mouse_send_direct(&r);
        sent = true;
    }
    if (sent) {
        wait_mouse_endpoint_idle();
    }
    if (scroll_out_pending()) {
        mouse_request_send();
    }
}

static uint8_t held_mods(void) {
    uint8_t mods = get_mods() | get_weak_mods();
#ifndef NO_ACTION_ONESHOT
    mods |= get_oneshot_mods();
#endif
    return mods;
}

/* val detents, direction already in the sign. */
static void scroll(int16_t val, bool vertical) {
    if (val == 0) {
        return;
    }
    /* Only modifiers this keyboard sends are visible here. */
    bool now = held_mods() != 0;
    scroll_out_detents(val, vertical, now);
    if (now) {
        send_urgent(false);
    } else {
        mouse_request_send();
    }
}

/* One detent the way QMK assigns to the WH_* keycode kc. */
static void scroll_as(uint16_t kc) {
    scroll(kc_positive(kc) ? 1 : -1, kc_vertical(kc));
}

/* One tap of a wheel cell that holds the WH_* key kc. */
static void scroll_from_cell(uint16_t cell_kc, uint16_t kc) {
    int16_t detent = cell_detent(cell_kc);
    if (kc == cell_kc) {
        scroll(detent, kc_vertical(kc)); /* as the device sent it */
    } else if (kc_vertical(kc) == kc_vertical(cell_kc)) {
        /* Remapped within the axis: the only meaning is "invert". */
        scroll(kc_positive(kc) == kc_positive(cell_kc) ? detent : (int16_t)-detent, kc_vertical(kc));
    } else {
        scroll_as(kc);
    }
}

/* A wheel sends a detent or two per report; this only bounds what a
 * misbehaving device could pile up. */
#define BACKLOG_MAX 64

static void add_backlog(int32_t *backlog, int16_t v) {
    int32_t b = *backlog + v;
    *backlog  = b > BACKLOG_MAX ? BACKLOG_MAX : (b < -BACKLOG_MAX ? -BACKLOG_MAX : b);
}

void wheel_physical(int16_t wheel, int16_t pan) {
    add_backlog(&backlog_wheel, wheel);
    add_backlog(&backlog_pan, pan);
}

/* Taps the cell for up to *budget detents of *backlog. */
static void tap_cells(int32_t *backlog, bool vertical, uint8_t *budget) {
    while (*backlog != 0 && *budget > 0) {
        uint16_t kc  = cell_kc_for(*backlog, vertical);
        keypos_t key = identity_pos(kc);
        uint16_t t   = timer_read() | 1;
        mouse_inject_begin();
        action_exec((keyevent_t){.key = key, .type = KEY_EVENT, .pressed = true, .time = t});
        action_exec((keyevent_t){.key = key, .type = KEY_EVENT, .pressed = false, .time = t});
        mouse_inject_end();
        *backlog -= cell_detent(kc);
        (*budget)--;
    }
}

/* One tap per detent, like an encoder: whatever the cell holds runs once per
 * click, and a tap QMK holds back (tap-hold deciding) still means exactly one
 * detent when it comes out. A fast spin taps over a few scans. */
void wheel_scan_end(void) {
    uint8_t budget = TAPS_PER_SCAN;
    tap_cells(&backlog_wheel, true, &budget);
    tap_cells(&backlog_pan, false, &budget);
}

bool wheel_is_cell_event(const keyrecord_t *record) {
    return IS_KEYEVENT(record->event) && cell_at(record->event.key) != KC_NO;
}

bool wheel_process_record(uint16_t keycode, keyrecord_t *record) {
    bool pressed = record->event.pressed;
    if (wheel_is_cell_event(record)) {
        /* Only wheel_scan_end taps these cells (no device sends those usages
         * as keys). A WH_* key there scrolls one detent per tap, without
         * repeat; anything else is QMK's to run. */
        if (!IS_MOUSEKEY_WHEEL(keycode)) {
            return true;
        }
        if (pressed) {
            scroll_from_cell(cell_at(record->event.key), keycode);
        }
        return false;
    }
    if (!IS_MOUSEKEY_WHEEL(keycode)) {
        return true;
    }
    /* A key mapped to WH_*: one detent now, then QMK's wheel repeat timing
     * (delay and interval from QMK Settings), one detent per step. */
    if (pressed) {
        scroll_as(keycode);
        repeat_kc      = keycode;
        repeat_since   = timer_read();
        repeat_started = false;
    } else if (repeat_kc == keycode) {
        repeat_kc = KC_NO;
    }
    return false;
}

void wheel_task(void) {
#ifdef MOUSEKEY_ENABLE
    if (repeat_kc == KC_NO) {
        return;
    }
    uint16_t wait = repeat_started ? (uint16_t)mk_wheel_interval : (uint16_t)(mk_wheel_delay * 10);
    if (timer_elapsed(repeat_since) < wait) {
        return;
    }
    repeat_started = true;
    repeat_since   = timer_read();
    scroll_as(repeat_kc);
#endif
}

void wheel_reset(void) {
    backlog_wheel = 0;
    backlog_pan   = 0;
    repeat_kc     = KC_NO;
}

/* patches/0003: QMK's own mousekey reports come here instead of going
 * straight to the PC: WH_* with a modifier (LCTL(WH_UP)), macros, MS_* keys.
 * Only this report's own motion and wheel go out, right away, because a
 * modified key releases its modifier just after this returns; button
 * changes go through the regular send, which keeps track of what the PC
 * has. */
void mousekey_host_send(report_mouse_t *report) {
    if (report == NULL) {
        return;
    }
    scroll_out_detents(report->v, true, true);
    scroll_out_detents(report->h, false, true);
    report_mouse_t r = {.x = report->x, .y = report->y};
    scroll_out_take_urgent(&r.v, &r.h);
    mouse_send_direct(&r);
    send_urgent(r.v != 0 || r.h != 0);
    mouse_request_send();
}
