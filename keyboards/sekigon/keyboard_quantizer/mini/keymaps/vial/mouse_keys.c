// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Key events for the mouse keycodes, and the rules for what ends a oneshot.
 *
 * "The next key" ends O.SCR / O.GES / O.SLO: a key press, a tap of LT/MT
 * (decided on release), or a tap dance press. It does not count when it is
 * one of the mouse's own controls (settings keys, modifier locks, WH_*,
 * layer keys, the O.* keys themselves) or our own taps (wheel cells,
 * gestures). Tap-latched toggles and the S.SCR TAP latch end with it too,
 * unless it is a mouse action (a click, a drag lock) or another mode key. */

#include QMK_KEYBOARD_H

#include <string.h>

#include "vial.h"
#include "dynamic_keymap.h"

#include "mouse_keys.h"
#include "kq_keycodes.h"
#include "locks.h"
#include "modes.h"
#include "mouse.h"
#include "settings.h"
#include "wheel.h"

typedef struct {
    uint16_t keycode;
    uint16_t inner; /* LT/MT tap keycode once QMK decided tap, else keycode */
    bool     pressed;
    bool     magic;    /* tap dance / combo, not a matrix key */
    bool     injected; /* our own tap */
    uint8_t  tap_count;
#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
    bool                   td_read;
    bool                   td_valid;
    vial_tap_dance_entry_t td;
#endif
} key_ctx_t;

typedef struct {
    uint16_t  kc;
    kq_mode_t mode;
    enum { KIND_HOLD, KIND_TOGGLE, KIND_ONESHOT } kind;
} mode_key_t;

static const mode_key_t mode_keys[] = {
    {U_SCR, KQ_MODE_SCROLL, KIND_HOLD},   {U_DSCR, KQ_MODE_SCROLL, KIND_TOGGLE},   {U_OSCR, KQ_MODE_SCROLL, KIND_ONESHOT},
    {U_GES, KQ_MODE_GESTURE, KIND_HOLD},  {U_TGES, KQ_MODE_GESTURE, KIND_TOGGLE},  {U_OGES, KQ_MODE_GESTURE, KIND_ONESHOT},
    {U_SLOW, KQ_MODE_SLOW, KIND_HOLD},    {U_TSLO, KQ_MODE_SLOW, KIND_TOGGLE},     {U_OSLO, KQ_MODE_SLOW, KIND_ONESHOT},
};

static const struct {
    uint16_t  kc;
    setting_t setting;
    int8_t    dir;
} adjusters[] = {
    /* A smaller divisor scrolls faster, a smaller step fires sooner. */
    {U_SCR_FASTER, SETTING_SCROLL_DIV, -1},   {U_SCR_SLOWER, SETTING_SCROLL_DIV, +1},
    {U_THR_EASIER, SETTING_GESTURE_STEP, -1}, {U_THR_HARDER, SETTING_GESTURE_STEP, +1},
    {U_DPI_FASTER, SETTING_CURSOR_SCALE, +1}, {U_DPI_SLOWER, SETTING_CURSOR_SCALE, -1},
};

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static const mode_key_t *mode_key(uint16_t kc) {
    for (uint8_t i = 0; i < LEN(mode_keys); i++) {
        if (mode_keys[i].kc == kc) {
            return &mode_keys[i];
        }
    }
    return NULL;
}

static bool is_mode_key(uint16_t kc) {
    return mode_key(kc) != NULL;
}

static bool is_oneshot_mode_key(uint16_t kc) {
    const mode_key_t *m = mode_key(kc);
    return m != NULL && m->kind == KIND_ONESHOT;
}

/* S.SCR, the saved values and SHOW / RST / CLR. Drag locks are mouse
 * actions, not settings. */
static bool is_settings_key(uint16_t kc) {
    switch (kc) {
        case U_SSCR:
        case U_SCR_FASTER:
        case U_SCR_SLOWER:
        case U_THR_EASIER:
        case U_THR_HARDER:
        case U_DPI_FASTER:
        case U_DPI_SLOWER:
        case U_SHOW:
        case U_RST:
        case U_CLR:
            return true;
        default:
            return false;
    }
}

static bool is_layer_key(uint16_t kc) {
    return IS_QK_MOMENTARY(kc) || IS_QK_TO(kc) || IS_QK_TOGGLE_LAYER(kc) || IS_QK_LAYER_TAP_TOGGLE(kc) ||
           IS_QK_DEF_LAYER(kc) || IS_QK_LAYER_MOD(kc) || IS_QK_ONE_SHOT_LAYER(kc);
}

/* Keys that operate the mouse rather than use it: never "the next key". */
static bool is_mouse_control_key(uint16_t kc) {
    return is_settings_key(kc) || locks_is_mod_key(kc) || IS_MOUSEKEY_WHEEL(kc) || is_layer_key(kc);
}

static bool is_oneshot_control_key(uint16_t kc) {
    return is_mode_key(kc) || is_mouse_control_key(kc);
}

/* LT/MT store an 8-bit tap key; once QMK decided tap, LT(1, Mouse1) is a
 * click like Mouse1. */
static uint16_t inner_keycode(uint16_t kc, const keyrecord_t *record) {
    if (record->tap.count == 0) {
        return kc;
    }
    if (IS_QK_LAYER_TAP(kc)) {
        return QK_LAYER_TAP_GET_TAP_KEYCODE(kc);
    }
    if (IS_QK_MOD_TAP(kc)) {
        return QK_MOD_TAP_GET_TAP_KEYCODE(kc);
    }
    return kc;
}

static void ctx_init(key_ctx_t *ctx, uint16_t keycode, keyrecord_t *record) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->keycode   = keycode;
    ctx->inner     = inner_keycode(keycode, record);
    ctx->pressed   = record->event.pressed;
    ctx->magic     = (record->event.key.row == VIAL_MATRIX_MAGIC && record->event.key.col == VIAL_MATRIX_MAGIC) ||
                 IS_COMBOEVENT(record->event);
    ctx->injected  = mouse_injecting() || wheel_is_injected(record);
    ctx->tap_count = record->tap.count;
}

#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
/* The tap dance entry, read from EEPROM at most once per event. */
static const vial_tap_dance_entry_t *ctx_tap_dance(key_ctx_t *ctx) {
    if (!ctx->td_read) {
        ctx->td_read  = true;
        ctx->td_valid = IS_QK_TAP_DANCE(ctx->keycode) &&
                        dynamic_keymap_get_tap_dance((uint8_t)(ctx->keycode - QK_TAP_DANCE), &ctx->td) == 0;
    }
    return ctx->td_valid ? &ctx->td : NULL;
}
#endif

/* A tap dance whose TAP (or double tap, or HOLD when it has no TAP) is a
 * mouse control does not count as the next key. */
static bool tap_dance_is_control(key_ctx_t *ctx) {
#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
    const vial_tap_dance_entry_t *td = ctx_tap_dance(ctx);
    if (td == NULL) {
        return false;
    }
    if (is_oneshot_control_key(td->on_tap) || is_oneshot_control_key(td->on_double_tap)) {
        return true;
    }
    return td->on_tap == KC_NO && is_oneshot_control_key(td->on_hold);
#else
    (void)ctx;
    return false;
#endif
}

static bool tap_dance_taps_button(key_ctx_t *ctx) {
#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
    const vial_tap_dance_entry_t *td = ctx_tap_dance(ctx);
    return td != NULL && IS_MOUSEKEY_BUTTON(td->on_tap);
#else
    (void)ctx;
    return false;
#endif
}

static bool is_click(key_ctx_t *ctx) {
    return IS_MOUSEKEY_BUTTON(ctx->inner) || IS_MOUSEKEY_BUTTON(ctx->keycode) || tap_dance_taps_button(ctx);
}

/* Clicking and locking a button down both leave a tap-latched toggle on.
 * Kept apart from is_click(), which also decides whether to spend a
 * oneshot modifier: a drag lock is consumed before the button is held, so
 * it must not drop the modifier mid-drag. */
static bool is_mouse_action(key_ctx_t *ctx) {
    return is_click(ctx) || locks_is_drag_key(ctx->keycode) || locks_is_drag_key(ctx->inner);
}

/* Regular keys count on press. LT/MT on release once QMK decided tap (a
 * hold is not a next key). A tap dance on press, unless it is a control. */
static bool is_next_key(key_ctx_t *ctx) {
    if (IS_QK_TAP_DANCE(ctx->keycode)) {
        return ctx->pressed && !tap_dance_is_control(ctx);
    }
    if (IS_QK_LAYER_TAP(ctx->keycode) || IS_QK_MOD_TAP(ctx->keycode)) {
        return !ctx->pressed && ctx->tap_count > 0;
    }
    return ctx->pressed;
}

/* QMK leaves a oneshot layer on after a key it did not process (ours return
 * false) and after modifier keys; end it here. Run after QMK's own
 * processing so the key still saw the layer. */
static void end_qmk_oneshots(key_ctx_t *ctx) {
    if (!is_next_key(ctx) || is_layer_key(ctx->inner) || is_layer_key(ctx->keycode) || tap_dance_is_control(ctx)) {
        return;
    }
#ifndef NO_ACTION_ONESHOT
    /* Keeps the layer if OSL is still held or tap-toggle locked. */
    clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
    if (is_click(ctx)) {
        clear_oneshot_mods();
    }
#endif
}

static void end_mode_oneshots(key_ctx_t *ctx) {
    if (!is_next_key(ctx) || ctx->injected || is_oneshot_mode_key(ctx->inner) || is_mouse_control_key(ctx->inner)) {
        return;
    }
    modes_end_oneshots();
    if (!is_mode_key(ctx->inner) && !is_mode_key(ctx->keycode) && !is_mouse_action(ctx)) {
        modes_end_magic_latches();
    }
}

static bool process_mode_key(const key_ctx_t *ctx) {
    if (ctx->keycode == U_SSCR) {
        modes_slow_scroll_key(ctx->pressed, ctx->magic);
        return true;
    }
    const mode_key_t *m = mode_key(ctx->keycode);
    if (m == NULL) {
        return false;
    }
    switch (m->kind) {
        case KIND_HOLD:
            modes_hold_key(m->mode, ctx->pressed, ctx->magic);
            break;
        case KIND_TOGGLE:
            modes_toggle_key(m->mode, ctx->pressed, ctx->magic);
            break;
        case KIND_ONESHOT:
            modes_oneshot_key(m->mode, ctx->pressed);
            break;
    }
    return true;
}

static bool process_settings_key(const key_ctx_t *ctx) {
    for (uint8_t i = 0; i < LEN(adjusters); i++) {
        if (adjusters[i].kc == ctx->keycode) {
            if (ctx->pressed) {
                settings_step(adjusters[i].setting, adjusters[i].dir);
            }
            return true;
        }
    }
    switch (ctx->keycode) {
        case U_SHOW:
            if (ctx->pressed) {
                mouse_print_settings_string();
            }
            return true;
        case U_RST:
            if (ctx->pressed) {
                mouse_reset_settings_and_latches();
            }
            return true;
        case U_CLR:
            if (ctx->pressed) {
                mouse_reset_latches();
            }
            return true;
        default:
            return false;
    }
}

bool process_record_mouse(uint16_t keycode, keyrecord_t *record) {
    if (record == NULL) {
        return true;
    }
    key_ctx_t ctx;
    ctx_init(&ctx, keycode, record);

    end_mode_oneshots(&ctx);

    bool handled = !wheel_process_record(keycode, record) || locks_process_key(keycode, ctx.pressed);
    if (!handled) {
        locks_note_key(keycode, ctx.pressed);
        handled = process_mode_key(&ctx) || process_settings_key(&ctx);
    }
    if (handled || IS_QK_KB(keycode)) {
        /* QMK will not process this key, so its oneshots are ours to end. */
        end_qmk_oneshots(&ctx);
        return false;
    }
    return true;
}

void post_process_record_mouse(uint16_t keycode, keyrecord_t *record) {
    if (record == NULL || IS_QK_KB(keycode) || IS_MOUSEKEY_WHEEL(keycode)) {
        return;
    }
    key_ctx_t ctx;
    ctx_init(&ctx, keycode, record);
    end_qmk_oneshots(&ctx);
}
