// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "pointing_device.h"
#ifdef MOUSEKEY_ENABLE
#    include "mousekey.h"
#endif
#include "action_tapping.h"
#ifdef COMBO_ENABLE
#    include "process_combo.h"
#endif

#include "mouse.h"
#include "ball.h"
#include "locks.h"
#include "modes.h"
#include "scroll_out.h"
#include "settings.h"
#include "wheel.h"

static bool    send_wanted;
static uint8_t sent_buttons;
static int32_t pending_x;
static int32_t pending_y;
static uint8_t inject_depth;

/* ---------------------------------------------------------------- send */

void mouse_request_send(void) {
    send_wanted = true;
}

void mouse_add_motion(int32_t x, int32_t y) {
    if (x != 0 || y != 0) {
        pending_x += x;
        pending_y += y;
        send_wanted = true;
    }
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

/* Builds and sends the regular report if it says anything new; what did not
 * fit waits for the next one. */
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

/* ---------------------------------------------------------- lifecycle */

void mouse_reset_latches(void) {
    modes_reset();
    locks_clear();
    wheel_reset();
    scroll_out_reset();
    ball_reset();
    pending_x   = 0;
    pending_y   = 0;
    send_wanted = true; /* drag-lock buttons are gone */
}

void mouse_reset_settings_and_latches(void) {
    mouse_reset_latches();
    settings_reset();
}

void mouse_init(void) {
    ball_init();
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
    *p++    = ball_mode_letter();
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
    printf("mode %c  s%u t%u d%u  slow %u  lock L%u M%u R%u  mod C%u S%u A%u W%u\n", ball_mode_letter(),
           (unsigned)settings_get(SETTING_SCROLL_DIV), (unsigned)settings_get(SETTING_GESTURE_STEP),
           (unsigned)settings_get(SETTING_CURSOR_SCALE), (unsigned)modes_active(KQ_MODE_SLOW), (unsigned)locks_drag(0),
           (unsigned)locks_drag(1), (unsigned)locks_drag(2), (unsigned)locks_mod(0), (unsigned)locks_mod(1),
           (unsigned)locks_mod(2), (unsigned)locks_mod(3));
}
