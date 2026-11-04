// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "layer_lock.h"
#include "kq_keycodes.h"

static layer_state_t locked;

layer_state_t layer_lock_locked(void) {
    return locked;
}

/* Every change of layer_state goes through here: locked layers stay on. */
layer_state_t layer_state_set_user(layer_state_t state) {
    return state | locked;
}

static void unlock(layer_state_t layers) {
    if (locked & layers) {
        locked &= ~layers;
        layer_and(~layers);
    }
}

void layer_lock_clear(void) {
    unlock(locked);
}

bool layer_lock_process_key(uint16_t keycode, bool pressed) {
    if (keycode != U_LLOCK) {
        if (pressed && IS_QK_TO(keycode)) {
            /* TO(n) leaves only n on: nothing stays locked. */
            locked = 0;
        } else if (pressed && IS_QK_TOGGLE_LAYER(keycode)) {
            layer_state_t bit = (layer_state_t)1 << QK_TOGGLE_LAYER_GET_LAYER(keycode);
            /* TG(n) of a locked layer turns it off: let it. */
            locked &= ~bit;
        }
        return false;
    }
    if (!pressed) {
        return true;
    }
    uint8_t layer = get_highest_layer(layer_state);
    if (layer == 0) {
        return true; /* nothing above the base layer to keep */
    }
    layer_state_t bit = (layer_state_t)1 << layer;
    if (locked & bit) {
        unlock(bit);
    } else {
        locked |= bit;
    }
    return true;
}
