// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "switcher.h"
#include "kq_keycodes.h"

#define ALT MOD_BIT(KC_LALT)

static bool          active;
static layer_state_t layers; /* the layers on at the first press */
static uint32_t      last_press;

bool switcher_active(void) {
    return active;
}

void switcher_clear(void) {
    if (active) {
        active = false;
        unregister_mods(ALT);
    }
}

bool switcher_process_key(uint16_t keycode, bool pressed) {
    if (keycode != U_ALT_TAB && keycode != U_ALT_STAB) {
        if (pressed) {
            switcher_clear();
        }
        return false;
    }
    if (!pressed) {
        return true;
    }
    /* Alt held by someone else (A.LCK, the Alt key) stays theirs. */
    if (!active && !(get_mods() & (ALT | MOD_BIT(KC_RALT)))) {
        active = true;
        layers = layer_state;
        register_mods(ALT);
    }
    last_press = timer_read32();
    tap_code16(keycode == U_ALT_TAB ? KC_TAB : S(KC_TAB));
    return true;
}

void switcher_task(void) {
    if (!active) {
        return;
    }
    bool layer_gone = layers != 0 && (layer_state & layers) != layers;
    bool timed_out  = layers == 0 && timer_elapsed32(last_press) >= SWITCHER_TIMEOUT_MS;
    if (layer_gone || timed_out) {
        switcher_clear();
    }
}
