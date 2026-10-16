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
#define CELL_QUEUE 8
#define URGENT_MAX_REPORTS 512
#define ENDPOINT_WAIT_MS 10

_Static_assert(KC_MS_WH_DOWN == KC_MS_WH_UP + 1 && KC_MS_WH_LEFT == KC_MS_WH_UP + 2 && KC_MS_WH_RIGHT == KC_MS_WH_UP + 3,
               "wheel keycodes are consecutive");

/* Wheel movement collected during a scan, as the device sent it. */
static int32_t batch_wheel;
static int32_t batch_pan;

/* Taps on a wheel cell that QMK has not handled yet, with the amount each
 * one carries. QMK may hold a tap back (tap-hold deciding), so the amount
 * waits here, matched to the tap by cell and order, not by timing. */
static struct {
    int16_t amount[CELL_QUEUE];
    uint8_t head;
    uint8_t count;
    uint8_t releases; /* injected releases still to come */
} cells[CELLS];

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

static int cell_at(keypos_t k) {
    for (int i = 0; i < CELLS; i++) {
        if (identity_is(k, (uint16_t)(KC_MS_WH_UP + i))) {
            return i;
        }
    }
    return -1;
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

/* A key mapped to WH_* scrolls the way QMK assigns to that keycode. */
static void scroll_as(uint16_t kc, int16_t magnitude) {
    int16_t mag = magnitude < 0 ? (int16_t)(magnitude == INT16_MIN ? INT16_MAX : -magnitude) : magnitude;
    scroll(kc_positive(kc) ? mag : (int16_t)-mag, kc_vertical(kc));
}

/* What a physical movement does when its cell holds the WH_* key kc. */
static void scroll_from_cell(uint16_t cell_kc, uint16_t kc, int16_t amount) {
    if (kc == cell_kc) {
        scroll(amount, kc_vertical(kc)); /* as the device sent it */
    } else if (kc_vertical(kc) == kc_vertical(cell_kc)) {
        /* Remapped within the axis: the only meaning is "invert". */
        scroll(kc_positive(kc) == kc_positive(cell_kc) ? amount : (int16_t)-amount, kc_vertical(kc));
    } else {
        scroll_as(kc, amount);
    }
}

void wheel_physical(int16_t wheel, int16_t pan) {
    batch_wheel += wheel;
    batch_pan += pan;
}

static void tap_cell(int32_t delta, bool vertical) {
    if (delta == 0) {
        return;
    }
    int16_t  amount = (int16_t)(delta > INT16_MAX ? INT16_MAX : (delta < -INT16_MAX ? -INT16_MAX : delta));
    uint16_t kc     = cell_kc_for(delta, vertical);
    int      c      = (int)(kc - KC_MS_WH_UP);
    if (cells[c].count == CELL_QUEUE) {
        /* Taps are backed up: add to the newest one instead of another. */
        uint8_t last  = (uint8_t)((cells[c].head + cells[c].count - 1) % CELL_QUEUE);
        int32_t sum   = (int32_t)cells[c].amount[last] + amount;
        cells[c].amount[last] = (int16_t)(sum > INT16_MAX ? INT16_MAX : (sum < -INT16_MAX ? -INT16_MAX : sum));
        return;
    }
    cells[c].amount[(cells[c].head + cells[c].count) % CELL_QUEUE] = amount;
    cells[c].count++;
    cells[c].releases++;

    keypos_t key = identity_pos(kc);
    uint16_t t   = timer_read() | 1;
    mouse_inject_begin();
    action_exec((keyevent_t){.key = key, .type = KEY_EVENT, .pressed = true, .time = t});
    action_exec((keyevent_t){.key = key, .type = KEY_EVENT, .pressed = false, .time = t});
    mouse_inject_end();
}

void wheel_scan_end(void) {
    int32_t w = batch_wheel, p = batch_pan;
    batch_wheel = 0;
    batch_pan   = 0;
    tap_cell(w, true);
    tap_cell(p, false);
}

bool wheel_is_injected(const keyrecord_t *record) {
    int c = cell_at(record->event.key);
    if (c < 0 || !IS_KEYEVENT(record->event)) {
        return false;
    }
    return record->event.pressed ? cells[c].count > 0 : cells[c].releases > 0;
}

static void repeat_start(uint16_t kc) {
    repeat_kc      = kc;
    repeat_since   = timer_read();
    repeat_started = false;
}

bool wheel_process_record(uint16_t keycode, keyrecord_t *record) {
    bool pressed = record->event.pressed;
    if (wheel_is_injected(record)) {
        int c = cell_at(record->event.key);
        if (pressed) {
            int16_t amount = cells[c].amount[cells[c].head];
            cells[c].head  = (uint8_t)((cells[c].head + 1) % CELL_QUEUE);
            cells[c].count--;
            if (IS_MOUSEKEY_WHEEL(keycode)) {
                scroll_from_cell((uint16_t)(KC_MS_WH_UP + c), keycode, amount);
                return false;
            }
            return true; /* something else is mapped there: QMK taps it */
        }
        cells[c].releases--;
        return !IS_MOUSEKEY_WHEEL(keycode);
    }
    if (!IS_MOUSEKEY_WHEEL(keycode)) {
        return true;
    }
    /* A key mapped to WH_*: one detent now, then QMK's wheel repeat timing
     * (delay and interval from QMK Settings), one detent per step. */
    if (pressed) {
        scroll_as(keycode, 1);
        repeat_start(keycode);
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
    scroll_as(repeat_kc, 1);
#endif
}

void wheel_reset(void) {
    batch_wheel = 0;
    batch_pan   = 0;
    repeat_kc   = KC_NO;
    /* Taps still inside QMK keep their queue entries: they will arrive. */
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
