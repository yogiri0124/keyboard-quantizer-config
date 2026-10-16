// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "locks.h"
#include "kq_keycodes.h"
#include "mouse.h"

typedef struct {
    uint16_t lock_kc;
    uint16_t button_kc;
    uint8_t  hid_button;
    bool     locked;
} drag_lock_t;

/* CLI order L / M / R. */
static drag_lock_t drag[DRAG_LOCK_COUNT] = {
    {U_DLCK, KC_MS_BTN1, MOUSE_BTN1, false},
    {U_DMID, KC_MS_BTN3, MOUSE_BTN3, false},
    {U_DRCK, KC_MS_BTN2, MOUSE_BTN2, false},
};

typedef struct {
    uint16_t lock_kc;
    uint16_t mod_kc;
    uint8_t  mod;
    bool     locked;
    bool     held; /* the modifier key itself is down */
} mod_lock_t;

/* CLI order C / S / A / W. */
static mod_lock_t mods[MOD_LOCK_COUNT] = {
    {U_LCTL_LOCK, KC_LCTL, MOD_BIT(KC_LCTL), false, false},
    {U_LSFT_LOCK, KC_LSFT, MOD_BIT(KC_LSFT), false, false},
    {U_LALT_LOCK, KC_LALT, MOD_BIT(KC_LALT), false, false},
    {U_LGUI_LOCK, KC_LGUI, MOD_BIT(KC_LGUI), false, false},
};

static drag_lock_t *drag_by_lock(uint16_t kc) {
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        if (drag[i].lock_kc == kc) {
            return &drag[i];
        }
    }
    return NULL;
}

static mod_lock_t *mod_by_lock(uint16_t kc) {
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        if (mods[i].lock_kc == kc) {
            return &mods[i];
        }
    }
    return NULL;
}

bool locks_is_drag_key(uint16_t keycode) {
    return drag_by_lock(keycode) != NULL;
}

bool locks_is_mod_key(uint16_t keycode) {
    return mod_by_lock(keycode) != NULL;
}

bool locks_process_key(uint16_t keycode, bool pressed) {
    drag_lock_t *d = drag_by_lock(keycode);
    if (d != NULL) {
        if (pressed) {
            d->locked = !d->locked;
            mouse_request_send();
        }
        return true;
    }
    mod_lock_t *m = mod_by_lock(keycode);
    if (m != NULL) {
        if (pressed) {
            m->locked = !m->locked;
            if (m->locked) {
                register_mods(m->mod);
            } else if (!m->held) {
                unregister_mods(m->mod);
            }
        }
        return true;
    }
    return false;
}

void locks_note_key(uint16_t keycode, bool pressed) {
    /* With a Magic swap on (Alt/GUI, Ctrl/GUI) the key sends the swapped
     * modifier: match the lock of the modifier it really sends. */
    uint16_t sent = IS_MODIFIER_KEYCODE(keycode) ? keycode_config((uint8_t)keycode) : keycode;
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        if (mods[i].mod_kc == sent) {
            mods[i].held = pressed;
            if (pressed) {
                mods[i].locked = false;
            }
        }
    }
}

void locks_button_pressed(uint16_t keycode) {
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        if (drag[i].button_kc == keycode) {
            drag[i].locked = false;
        }
    }
}

uint8_t locks_buttons(void) {
    uint8_t b = 0;
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        if (drag[i].locked) {
            b |= drag[i].hid_button;
        }
    }
    return b;
}

bool locks_drag(uint8_t i) {
    return i < DRAG_LOCK_COUNT && drag[i].locked;
}

bool locks_mod(uint8_t i) {
    return i < MOD_LOCK_COUNT && mods[i].locked;
}

void locks_task(void) {
    /* Something (a clear_mods(), a macro) may have dropped a locked
     * modifier: put it back. */
    uint8_t need = 0;
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        if (mods[i].locked) {
            need |= mods[i].mod;
        }
    }
    uint8_t missing = (uint8_t)(need & ~get_mods());
    if (missing) {
        register_mods(missing);
    }
}

void locks_clear(void) {
    bool    had_drag = locks_buttons() != 0;
    uint8_t drop     = 0;
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        drag[i].locked = false;
    }
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        /* A modifier key still physically down stays down. */
        if (mods[i].locked && !mods[i].held) {
            drop |= mods[i].mod;
        }
        mods[i].locked = false;
    }
    if (drop) {
        unregister_mods(drop);
    }
    if (had_drag) {
        mouse_request_send();
    }
}
