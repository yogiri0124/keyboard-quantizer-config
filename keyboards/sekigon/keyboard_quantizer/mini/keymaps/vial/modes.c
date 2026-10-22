// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "modes.h"

#include <string.h>

/* Magic holders are paired press-to-release last-in-first-out: magic events
 * carry no identity (every tap dance and combo reports the same position),
 * and holds nest that way. Beyond this depth the deepest entry is reused,
 * which only affects the TAP / HOLD decision, never the count. */
#define MAGIC_HOLD_MAX 4

typedef struct {
    uint8_t  count;
    uint16_t press_pass[MAGIC_HOLD_MAX];
} magic_holds_t;

typedef struct {
    uint8_t       hold_phys;
    magic_holds_t hold_magic;
    bool          toggle;
    bool          toggle_magic; /* turned on by a tap dance TAP: ends with the next key */
    uint16_t      toggle_press_pass;
    bool          oneshot;
} mode_state_t;

static mode_state_t modes[KQ_MODE_COUNT];

static struct {
    uint8_t       hold_phys;
    magic_holds_t hold_magic;
    bool          latched; /* a tap dance TAP: until the next key */
} slow_scroll;

static uint16_t pass;

void modes_new_pass(void) {
    pass++;
}

static void count_phys(uint8_t *count, bool pressed) {
    if (pressed) {
        if (*count < UINT8_MAX) {
            (*count)++;
        }
    } else if (*count > 0) {
        (*count)--;
    }
}

static void magic_press(magic_holds_t *h) {
    if (h->count == UINT8_MAX) {
        return;
    }
    uint8_t slot        = h->count < MAGIC_HOLD_MAX ? h->count : MAGIC_HOLD_MAX - 1;
    h->press_pass[slot] = pass;
    h->count++;
}

/* Releases the latest magic holder; true if it was a TAP (pressed in this
 * same pass). False when there was no holder. */
static bool magic_release(magic_holds_t *h) {
    if (h->count == 0) {
        return false;
    }
    h->count--;
    uint8_t slot = h->count < MAGIC_HOLD_MAX ? h->count : MAGIC_HOLD_MAX - 1;
    return h->press_pass[slot] == pass;
}

static void arm_oneshot(kq_mode_t mode) {
    /* One oneshot at a time. */
    for (uint8_t i = 0; i < KQ_MODE_COUNT; i++) {
        modes[i].oneshot = (i == (uint8_t)mode);
    }
}

void modes_hold_key(kq_mode_t mode, bool pressed, bool magic) {
    mode_state_t *st = &modes[mode];
    if (!magic) {
        count_phys(&st->hold_phys, pressed);
        return;
    }
    if (pressed) {
        /* Counted even while the oneshot is armed, so the release pairs up. */
        magic_press(&st->hold_magic);
        return;
    }
    bool tap = magic_release(&st->hold_magic);
    /* Only the last holder letting go can turn the hold into a oneshot. */
    if (tap && st->hold_magic.count == 0) {
        arm_oneshot(mode);
    }
}

void modes_toggle_key(kq_mode_t mode, bool pressed, bool magic) {
    mode_state_t *st = &modes[mode];
    if (pressed) {
        st->toggle            = !st->toggle;
        st->toggle_magic      = false;
        st->toggle_press_pass = pass;
        return;
    }
    /* Decided on release, so a letter typed during a HOLD cannot end it. */
    if (magic) {
        st->toggle_magic = st->toggle && st->toggle_press_pass == pass;
    }
}

void modes_oneshot_key(kq_mode_t mode, bool pressed) {
    if (!pressed) {
        return;
    }
    if (modes[mode].oneshot) {
        modes[mode].oneshot = false;
    } else {
        arm_oneshot(mode);
    }
}

void modes_slow_scroll_key(bool pressed, bool magic) {
    if (!magic) {
        count_phys(&slow_scroll.hold_phys, pressed);
        return;
    }
    if (pressed) {
        magic_press(&slow_scroll.hold_magic);
    } else if (magic_release(&slow_scroll.hold_magic)) {
        slow_scroll.latched = !slow_scroll.latched;
    }
}

bool modes_active(kq_mode_t mode) {
    const mode_state_t *st = &modes[mode];
    return st->hold_phys != 0 || st->hold_magic.count != 0 || st->toggle || st->oneshot;
}

bool modes_slow_scroll_active(void) {
    return slow_scroll.latched || slow_scroll.hold_phys != 0 || slow_scroll.hold_magic.count != 0;
}

void modes_end_oneshots(void) {
    for (uint8_t i = 0; i < KQ_MODE_COUNT; i++) {
        modes[i].oneshot = false;
    }
}

void modes_end_magic_latches(void) {
    for (uint8_t i = 0; i < KQ_MODE_COUNT; i++) {
        if (modes[i].toggle_magic) {
            modes[i].toggle       = false;
            modes[i].toggle_magic = false;
        }
    }
    /* A key still holding S.SCR keeps it on through its own count. */
    slow_scroll.latched = false;
}

void modes_reset(void) {
    memset(modes, 0, sizeof(modes));
    memset(&slow_scroll, 0, sizeof(slow_scroll));
}
