// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Ball modes: scroll (SCR / D.SCR / O.SCR), gesture (GES / T.GES / O.GES)
 * and slow cursor (SLOW / T.SLO / O.SLO), plus slow scroll (S.SCR).
 *
 * Each mode is on while any of these holds it:
 * - hold:    a physical hold key is down (counted, so two keys overlap), or a
 *            tap dance / combo HOLD of it is;
 * - toggle:  the toggle key flipped it on;
 * - oneshot: the oneshot key armed it, until the next key.
 * A tap dance or combo TAP of a hold key arms the oneshot instead, and a TAP
 * of a toggle key lasts only until the next key (a "magic" toggle). What
 * counts as "the next key" is decided by the caller (mouse_keys.c).
 *
 * A magic key event cannot say whether it was a TAP or a HOLD; a TAP presses
 * and releases within one main-loop pass, a HOLD does not, so the pass number
 * (modes_new_pass) tells them apart. Pure C, tested on the host. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KQ_MODE_SCROLL = 0,
    KQ_MODE_GESTURE,
    KQ_MODE_SLOW,
    KQ_MODE_COUNT,
} kq_mode_t;

void modes_hold_key(kq_mode_t mode, bool pressed, bool magic);
void modes_toggle_key(kq_mode_t mode, bool pressed, bool magic);
void modes_oneshot_key(kq_mode_t mode, bool pressed);
void modes_slow_scroll_key(bool pressed, bool magic);

bool modes_active(kq_mode_t mode);
bool modes_slow_scroll_active(void);

/* The next key came: oneshots end. */
void modes_end_oneshots(void);
/* ...and, unless it was a mouse action, the magic toggles and the S.SCR TAP
 * latch end too. */
void modes_end_magic_latches(void);

void modes_new_pass(void); /* once per main-loop pass */
void modes_reset(void);
