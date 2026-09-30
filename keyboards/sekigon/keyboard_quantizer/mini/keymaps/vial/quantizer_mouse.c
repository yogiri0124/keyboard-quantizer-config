// Copyright 2023 sekigon-gonnoc
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
#include "report_parser.h"
#include "report_descriptor_parser.h"
#include "keymap.h"
#include "quantizer_mouse.h"
#include "scroll_out.h"
#include "via.h"
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
#    include "usb_main.h"
#endif

enum via_mouse_value {
    id_mouse_scroll_div         = 1,
    id_mouse_gesture_threshold  = 2,
    id_mouse_cursor_scale       = 3,
    id_mouse_settings_reset     = 4,
};

_Static_assert(id_mouse_scroll_div == 1 && id_mouse_gesture_threshold == 2 && id_mouse_cursor_scale == 3 &&
                   id_mouse_settings_reset == 4,
               "vial.json Mouse menu content[] value_ids must match via_mouse_value");

/* Slot order matches vial.json row 31: right, left, up, down. */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_RIGHT,
    GESTURE_LEFT,
    GESTURE_UP,
    GESTURE_DOWN,
} gesture_id_t;

#define GESTURE_SLOT_COUNT 4

extern matrix_row_t *matrix_dest;
extern bool          mouse_send_flag;

#define SCROLL_DIV_MIN USER_SCROLL_DIV_MIN
#define SCROLL_DIV_MAX USER_SCROLL_DIV_MAX
#define SCROLL_DIV_STEP 4
#define GESTURE_THRESH_MIN USER_GESTURE_THRESHOLD_MIN
#define GESTURE_THRESH_MAX USER_GESTURE_THRESHOLD_MAX
#define GESTURE_THRESH_STEP 10
#define GESTURE_ACTION_LAYER 1
#define GESTURE_COOLDOWN_MS 150
#define GESTURE_LOCK_IDLE_MS 80
#define GESTURE_ACCUM_IDLE_MS 200
#define SLOW_CURSOR_DIV 4
#define SLOW_SCROLL_MUL 4
#define CURSOR_SCALE_MIN USER_CURSOR_SCALE_MIN
#define CURSOR_SCALE_MAX USER_CURSOR_SCALE_MAX
#define CURSOR_SCALE_STEP 2
#define CURSOR_RECOIL_PREV 64
#define CURSOR_RECOIL_MAX 8
#define HID8_MIN (-127)
#define HID8_MAX 127
#define CURSOR_SCALE_UNIT 16
#define MAGIC_TAP_SLACK_MS 40
#define MAGIC_TAP_LIMIT_MAX_MS 200
#define CONFIG_SAVE_DEBOUNCE_MS 750

_Static_assert(SCROLL_DIV_MIN == 8 && SCROLL_DIV_MAX == 80, "vial.json scroll divisor range must match");
_Static_assert(CURSOR_SCALE_MIN == 4 && CURSOR_SCALE_MAX == 32, "vial.json cursor scale range must match");
_Static_assert(GESTURE_THRESH_MIN == 20 && GESTURE_THRESH_MAX == 120, "vial.json gesture threshold range must match");
_Static_assert(MATRIX_MSGES_ROW == 31, "vial orange gesture slots are matrix row 31");
_Static_assert(MATRIX_COLS == MATRIX_COLS_DEFAULT, "identity col count must match MATRIX_COLS_DEFAULT");
_Static_assert(KC_MS_BTN1 / MATRIX_COLS_DEFAULT + 1 == 27 && KC_MS_BTN1 % MATRIX_COLS_DEFAULT == 1,
               "identity Mouse1 is vial cell 27,1");
_Static_assert(KC_MS_WH_UP / MATRIX_COLS_DEFAULT + 1 == 28 && KC_MS_WH_UP % MATRIX_COLS_DEFAULT == 1,
               "identity WH_UP is vial cell 28,1");
/* vial.json draws 28,3 left of 28,4 in the HUGE PLUS view, so a tilt must fire
 * the cell on the side it was tilted towards. */
_Static_assert(KC_MS_WH_LEFT / MATRIX_COLS_DEFAULT + 1 == 28 && KC_MS_WH_LEFT % MATRIX_COLS_DEFAULT == 3,
               "identity WH_LEFT is vial cell 28,3");
_Static_assert(KC_MS_WH_RIGHT / MATRIX_COLS_DEFAULT + 1 == 28 && KC_MS_WH_RIGHT % MATRIX_COLS_DEFAULT == 4,
               "identity WH_RIGHT is vial cell 28,4");
_Static_assert(KC_MS_UP / MATRIX_COLS_DEFAULT + 1 == 26 && KC_MS_UP % MATRIX_COLS_DEFAULT == 5,
               "identity MS_UP is vial cell 26,5");
_Static_assert(GESTURE_SLOT_COUNT <= MATRIX_COLS_DEFAULT, "gesture slots must fit one matrix row");

/* The keymap is an identity matrix: HID usage u is edited in Vial at row
 * u / MATRIX_COLS_DEFAULT + 1, column u % MATRIX_COLS_DEFAULT. Row 0 mirrors the
 * modifier byte and row MATRIX_MSGES_ROW holds the gesture slots, which are
 * virtual and have no HID usage behind them. This arithmetic was open-coded in
 * six places using three different spellings (/8, /MATRIX_COLS_DEFAULT, &0x07). */
static uint8_t identity_row(uint16_t usage) {
    return (uint8_t)(usage / MATRIX_COLS_DEFAULT + 1);
}

static uint8_t identity_col(uint16_t usage) {
    return (uint8_t)(usage % MATRIX_COLS_DEFAULT);
}

static uint16_t gesture_slot(uint8_t index) {
    return (uint16_t)((MATRIX_MSGES_ROW - 1) * MATRIX_COLS_DEFAULT + index);
}

static bool          is_encoder_action           = false;
static int32_t       gesture_move_x              = 0;
static int32_t       gesture_move_y              = 0;
static int16_t       wheel_move_v                = 0;
static int16_t       wheel_move_h                = 0;
static uint8_t       ball_scroll_div             = USER_SCROLL_DIV_DEFAULT;
static uint8_t       mouse_cursor_scale          = USER_CURSOR_SCALE_DEFAULT;
static uint8_t       mouse_gesture_threshold     = USER_GESTURE_THRESHOLD_DEFAULT;
static uint16_t      gesture_last_fired_ms       = 0;
static uint16_t      gesture_idle_ms             = 0;
static gesture_id_t  gesture_dir_lock            = GESTURE_NONE;
static int32_t       cursor_prev_x               = 0;
static int32_t       cursor_prev_y               = 0;
static int32_t       wheel_batch_v               = 0;
static int32_t       wheel_batch_h               = 0;
static int16_t       cursor_scale_x_frac         = 0;
static int16_t       cursor_scale_y_frac         = 0;
static int16_t       slow_cursor_x_frac          = 0;
static int16_t       slow_cursor_y_frac          = 0;
static bool          skip_mode_oneshot_cancel    = false;
static bool          config_dirty                = false;
static uint16_t      config_dirty_ms             = 0;

/* Depth of the magic-hold timestamp stack. Two is already an unusual keymap;
 * beyond this the oldest timestamp is reused, which only affects the
 * tap-vs-hold decision, never the hold count itself. */
#define MODE_MAGIC_HOLD_MAX 4

typedef enum {
    MODE_SCROLL = 0,
    MODE_GESTURE,
    MODE_SLOW,
    MOUSE_MODE_COUNT,
} mouse_mode_id_t;

/* One latch set per mode, so the three modes cannot drift apart the way nine
 * separately named globals did. down_ms is per latch on purpose: two tap-dance
 * mode keys can overlap, and one shared timestamp made the release of one key
 * read the press time of the other and take the wrong tap-vs-hold branch. */
typedef struct {
    /* Several keys can hold the same mode at once: two physical keys on
     * different layers, or a physical key plus a tap dance. A single bool made
     * the first release drop the mode while the others were still down, so both
     * kinds of holder are counted. Every press has a matching release (QMK
     * caches the source layer for physical keys, tap dance pairs its own
     * up/down), and mouse_modes_reset() zeroes both on RST / CLR / unplug. */
    uint8_t  hold_phys;    /* physical SCR / GES / SLOW keys held */
    uint8_t  hold_magic;   /* tap dances or combos holding the same mode */
    bool     toggle;       /* D.SCR / T.GES / T.SLO latched */
    bool     oneshot;      /* O.SCR / O.GES / O.SLO armed */
    bool     toggle_magic; /* the toggle came from a tap dance TAP */
    /* One press time per magic holder, popped last-in-first-out. A single
     * shared timestamp let a later press overwrite the one an earlier holder
     * still needed for its own tap-vs-hold decision. Magic events carry no
     * identity (every tap dance and combo reports the same key position), so
     * LIFO is the only pairing available; it is also the order holds nest in. */
    uint16_t hold_magic_ms[MODE_MAGIC_HOLD_MAX];
    uint16_t toggle_down_ms;
} mouse_mode_state_t;

static mouse_mode_state_t mode_state[MOUSE_MODE_COUNT];

static bool mode_held(const mouse_mode_state_t *st) {
    return st->hold_phys != 0 || st->hold_magic != 0;
}

/* S.SCR is not a ball mode: it multiplies the scroll divisor while active, so
 * it keeps its own state instead of being squeezed into the table. The latch
 * and the holders are independent: a tap dance TAP flips the latch, every
 * press holds it down for as long as that key is down, and the two never
 * overwrite each other. */
static struct {
    bool     latched;    /* a tap dance TAP turned it on until the next key */
    uint8_t  hold_phys;  /* physical S.SCR keys held */
    uint8_t  hold_magic; /* tap dances or combos holding it */
    uint16_t hold_magic_ms[MODE_MAGIC_HOLD_MAX];
} slow_scroll;

static bool slow_scroll_is_active(void) {
    return slow_scroll.latched || slow_scroll.hold_phys != 0 || slow_scroll.hold_magic != 0;
}

static bool mode_active(mouse_mode_id_t id) {
    const mouse_mode_state_t *st = &mode_state[id];
    return mode_held(st) || st->toggle || st->oneshot;
}

typedef struct {
    uint16_t lock_kc;
    uint16_t mouse_kc;
    uint8_t  hid_button;
    bool     locked;
} drag_lock_t;

/* CLI order is L / M / R. */
static drag_lock_t drag_locks[] = {
    {U_DLCK, KC_MS_BTN1, MOUSE_BTN1, false},
    {U_DMID, KC_MS_BTN3, MOUSE_BTN3, false},
    {U_DRCK, KC_MS_BTN2, MOUSE_BTN2, false},
};

#define DRAG_LOCK_COUNT ((uint8_t)(sizeof(drag_locks) / sizeof(drag_locks[0])))

typedef struct {
    uint16_t lock_kc;
    uint16_t phys_kc;
    uint8_t  mod;
    bool     locked;
    bool     physically_held;
} mod_lock_t;

/* CLI order is C / S / A / W. */
static mod_lock_t mod_locks[] = {
    {U_LCTL_LOCK, KC_LCTL, MOD_BIT(KC_LCTL), false, false},
    {U_LSFT_LOCK, KC_LSFT, MOD_BIT(KC_LSFT), false, false},
    {U_LALT_LOCK, KC_LALT, MOD_BIT(KC_LALT), false, false},
    {U_LGUI_LOCK, KC_LGUI, MOD_BIT(KC_LGUI), false, false},
};

#define MOD_LOCK_COUNT ((uint8_t)(sizeof(mod_locks) / sizeof(mod_locks[0])))
_Static_assert(DRAG_LOCK_COUNT == 3, "CLI lock L/M/R order matches drag_locks[]");
_Static_assert(MOD_LOCK_COUNT == 4, "CLI mod C/S/A/W order matches mod_locks[]");

/* Read-only diagnostics for the "hid" CLI command. Nothing here feeds control
 * flow except last_button, which map_buttons_to_matrix() replays after a
 * keyboard report overwrites the shared matrix rows. */
static struct {
    uint16_t kb_count;
    uint16_t mouse_count;
    uint16_t consumer_count;
    uint16_t system_count;
    uint16_t vendor_count;
    uint16_t last_consumer;
    uint16_t last_system;
    uint16_t last_vendor;
    uint16_t last_undefined;
    uint8_t  last_button;
    int16_t  last_wheel; /* last non-zero Wheel, signed as the device sent it */
    int16_t  last_pan;   /* last non-zero AC Pan, signed as the device sent it */
} hid_stats;

static uint16_t timer_now(void) {
    return timer_read() | 1;
}

/* Elapsed ms since a timer_now() stamp. timer_now() runs up to 1 ms ahead (the
 * | 1 keeps 0 free for "unset"), and plain timer_elapsed() on it reads 65535
 * within that millisecond. */
static uint16_t timer_since(uint16_t stamp) {
    uint16_t d = (uint16_t)(timer_read() - stamp);
    return d == UINT16_MAX ? 0 : d;
}

static int32_t abs32(int32_t value) {
    return value < 0 ? -value : value;
}

static int32_t clamp_i32(int32_t value, int32_t lo, int32_t hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static uint8_t clamp_u8(int16_t value, uint8_t lo, uint8_t hi) {
    return (uint8_t)clamp_i32(value, lo, hi);
}

static int8_t clamp_hid8(int32_t value) {
    return (int8_t)clamp_i32(value, HID8_MIN, HID8_MAX);
}

/* Host wheel counts per detent: 1 until Windows/Linux turn on the resolution
 * multiplier (patches/0002), then MOUSE_WHEEL_RESOLUTION_MULTIPLIER. */
static int32_t wheel_multiplier(bool vertical) {
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
    return usb_mouse_wheel_multiplier(vertical);
#else
    (void)vertical;
    return 1;
#endif
}

static mouse_xy_report_t clamp_xy(int32_t value) {
    return (mouse_xy_report_t)clamp_i32(value, XY_REPORT_MIN, XY_REPORT_MAX);
}

static bool gesture_default_from_keymap(void);

static bool cursor_is_slow(void) {
    return mode_active(MODE_SLOW);
}

static bool ball_is_scroll(void) {
    return mode_active(MODE_SCROLL);
}

static bool ball_is_gesture(void) {
    return mode_active(MODE_GESTURE) || gesture_default_from_keymap();
}

static void clear_slow_cursor_frac_if_idle(void) {
    if (!cursor_is_slow()) {
        slow_cursor_x_frac = 0;
        slow_cursor_y_frac = 0;
    }
}

static void clear_gesture_motion(void) {
    gesture_move_x   = 0;
    gesture_move_y   = 0;
    gesture_dir_lock = GESTURE_NONE;
    gesture_idle_ms  = 0;
}

static void clear_gesture_accum_if_idle(void) {
    if (!ball_is_gesture()) {
        clear_gesture_motion();
    }
}

static void clear_cursor_scale_frac(void) {
    cursor_scale_x_frac = 0;
    cursor_scale_y_frac = 0;
}

static void clear_cursor_recoil(void) {
    cursor_prev_x = 0;
    cursor_prev_y = 0;
}

/* Scroll, gesture and slow-cursor are one state machine instantiated three
 * times: a hold latch, a toggle latch, a oneshot latch, and the tap-dance MAGIC
 * flags for hold and toggle. They used to be three hand-written copies of the
 * same logic, which is how they drifted apart. Only the two hooks below differ
 * per mode; everything else is shared. */
typedef struct {
    uint16_t kc_hold;
    uint16_t kc_toggle;
    uint16_t kc_oneshot;
    void (*on_enter)(void);  /* became active: drop history belonging to another mode */
    void (*on_settle)(void); /* may have gone idle: drop the leftovers of this mode */
} mouse_mode_def_t;

/* Entering scroll or gesture must drop a half-finished flick and the cursor
 * recoil history, or leftover ball motion fires in the new mode. Slow cursor
 * keeps both: it only rescales the same cursor stream. */
static void mode_drop_ball_history(void) {
    clear_gesture_motion();
    clear_cursor_recoil();
}

static const mouse_mode_def_t mode_defs[MOUSE_MODE_COUNT] = {
    [MODE_SCROLL]  = {U_SCR, U_DSCR, U_OSCR, mode_drop_ball_history, NULL},
    [MODE_GESTURE] = {U_GES, U_TGES, U_OGES, mode_drop_ball_history, clear_gesture_accum_if_idle},
    [MODE_SLOW]    = {U_SLOW, U_TSLO, U_OSLO, NULL, clear_slow_cursor_frac_if_idle},
};

static void mode_enter(mouse_mode_id_t id) {
    if (mode_defs[id].on_enter != NULL) {
        mode_defs[id].on_enter();
    }
}

static void mode_settle(mouse_mode_id_t id) {
    if (mode_defs[id].on_settle != NULL) {
        mode_defs[id].on_settle();
    }
}

static void clear_mode_fracs(void) {
    for (uint8_t i = 0; i < MOUSE_MODE_COUNT; i++) {
        mode_settle((mouse_mode_id_t)i);
    }
}

/* Returns MOUSE_MODE_COUNT when the keycode is not a mode key. */
static mouse_mode_id_t mode_for_key(uint16_t keycode) {
    for (uint8_t i = 0; i < MOUSE_MODE_COUNT; i++) {
        const mouse_mode_def_t *d = &mode_defs[i];
        if (keycode == d->kc_hold || keycode == d->kc_toggle || keycode == d->kc_oneshot) {
            return (mouse_mode_id_t)i;
        }
    }
    return MOUSE_MODE_COUNT;
}

/* Turning one oneshot on drops the others, then every mode settles its own
 * leftovers (a no-op for whichever mode is still active) before the new mode
 * clears the history it must not inherit. */
static void mode_oneshot_enter(mouse_mode_id_t target) {
    for (uint8_t i = 0; i < MOUSE_MODE_COUNT; i++) {
        mode_state[i].oneshot = (i == (uint8_t)target);
    }
    clear_mode_fracs();
    mode_enter(target);
}

static drag_lock_t *drag_lock_for_key(uint16_t keycode) {
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        if (drag_locks[i].lock_kc == keycode) {
            return &drag_locks[i];
        }
    }
    return NULL;
}

static drag_lock_t *drag_lock_for_mouse(uint16_t keycode) {
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        if (drag_locks[i].mouse_kc == keycode) {
            return &drag_locks[i];
        }
    }
    return NULL;
}

static mod_lock_t *mod_lock_for_key(uint16_t keycode) {
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        if (mod_locks[i].lock_kc == keycode) {
            return &mod_locks[i];
        }
    }
    return NULL;
}

static mod_lock_t *mod_lock_for_phys(uint16_t keycode) {
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        if (mod_locks[i].phys_kc == keycode) {
            return &mod_locks[i];
        }
    }
    return NULL;
}

static bool is_drag_lock_key(uint16_t keycode) {
    return drag_lock_for_key(keycode) != NULL;
}

static bool is_mod_lock_key(uint16_t keycode) {
    return mod_lock_for_key(keycode) != NULL;
}

static void clear_drag_locks(void) {
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        drag_locks[i].locked = false;
    }
}

static void clear_mod_locks(void) {
    uint8_t drop = 0;
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        /* Keep physically_held. CLR/RST/unplug must not pretend a still-down
         * Ctrl/Shift/Alt/GUI was released, or unregister_mods() drops it. */
        if (mod_locks[i].locked && !mod_locks[i].physically_held) {
            drop |= mod_locks[i].mod;
        }
        mod_locks[i].locked = false;
    }
    if (drop) {
        unregister_mods(drop);
    }
}

static void apply_mod_locks(void) {
    uint8_t need = 0;
    for (uint8_t i = 0; i < MOD_LOCK_COUNT; i++) {
        if (mod_locks[i].locked) {
            need |= mod_locks[i].mod;
        }
    }
    uint8_t missing = (uint8_t)(need & ~get_mods());
    if (missing) {
        register_mods(missing);
    }
}

static bool process_drag_lock_keycode(uint16_t keycode, bool pressed) {
    drag_lock_t *entry = drag_lock_for_key(keycode);
    if (entry == NULL) {
        return false;
    }
    if (pressed) {
        entry->locked   = !entry->locked;
        mouse_send_flag = true;
    }
    return true;
}

static bool process_mod_lock_keycode(uint16_t keycode, bool pressed) {
    mod_lock_t *entry = mod_lock_for_key(keycode);
    if (entry == NULL) {
        return false;
    }
    if (pressed) {
        entry->locked = !entry->locked;
        if (entry->locked) {
            register_mods(entry->mod);
        } else if (!entry->physically_held) {
            unregister_mods(entry->mod);
        }
    }
    return true;
}

static void note_physical_mod(uint16_t keycode, bool pressed) {
    mod_lock_t *entry = mod_lock_for_phys(keycode);
    if (entry == NULL) {
        return;
    }
    entry->physically_held = pressed;
    if (pressed) {
        entry->locked = false;
    }
}

static bool is_virtual_key_event(const keyrecord_t *record) {
    return (record->event.key.row == VIAL_MATRIX_MAGIC && record->event.key.col == VIAL_MATRIX_MAGIC) ||
           IS_COMBOEVENT(record->event);
}

static bool is_oneshot_mode_key(uint16_t keycode) {
    for (uint8_t i = 0; i < MOUSE_MODE_COUNT; i++) {
        if (keycode == mode_defs[i].kc_oneshot) {
            return true;
        }
    }
    return false;
}

static bool is_wheel_keycode(uint16_t keycode) {
    return IS_MOUSEKEY_WHEEL(keycode);
}

/* LT/MT store only an 8-bit tap key. After QMK decides tap, use that inner
 * key for oneshot cancel so LT(1, Mouse1) matches a plain click. */
static uint16_t tapping_inner_keycode(uint16_t keycode, const keyrecord_t *record) {
    if (record->tap.count == 0) {
        return keycode;
    }
    if (IS_QK_LAYER_TAP(keycode)) {
        return QK_LAYER_TAP_GET_TAP_KEYCODE(keycode);
    }
    if (IS_QK_MOD_TAP(keycode)) {
        return QK_MOD_TAP_GET_TAP_KEYCODE(keycode);
    }
    return keycode;
}

/* Everything derived from one key event, computed once per context. The tap
 * dance entry lives in EEPROM and the classification helpers below used to look
 * it up three to five times per event; it is now read at most once per context.
 * process_record_mouse() and post_process_record_mouse() build separate
 * contexts, so a pass-through tap dance still costs two reads per event. */
typedef struct {
    uint16_t keycode;
    uint16_t inner;  /* LT/MT tap keycode once QMK has decided tap, else keycode */
    bool     pressed;
    bool     magic;  /* tap dance / combo generated, not a physical matrix key */
    uint8_t  tap_count;
#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
    bool                   td_read;
    bool                   td_valid;
    vial_tap_dance_entry_t td;
#endif
} key_ctx_t;

static void key_ctx_init(key_ctx_t *ctx, uint16_t keycode, const keyrecord_t *record) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->keycode   = keycode;
    ctx->inner     = tapping_inner_keycode(keycode, record);
    ctx->pressed   = record->event.pressed;
    ctx->magic     = is_virtual_key_event(record);
    ctx->tap_count = record->tap.count;
}

/* Regular keys cancel on press. LT/MT press is not yet tap vs hold; a tap is
 * the release with count>0. Hold itself is not a "next key". Tap dance TAP of
 * a real key is handled after the TD entry is looked up. */
static bool mode_cancel_event(const key_ctx_t *ctx) {
    if (IS_QK_TAP_DANCE(ctx->keycode)) {
        return false;
    }
    if (IS_QK_LAYER_TAP(ctx->keycode) || IS_QK_MOD_TAP(ctx->keycode)) {
        return !ctx->pressed && ctx->tap_count > 0;
    }
    return ctx->pressed;
}

static bool is_mode_latch_key(uint16_t keycode) {
    return mode_for_key(keycode) != MOUSE_MODE_COUNT;
}

/* S.SCR, the saved sliders and SHOW/RST/CLR do not end O.SCR/O.GES/O.SLO.
 * Drag locks deliberately are not here: they are mouse actions, not settings. */
static bool is_mouse_settings_key(uint16_t keycode) {
    switch (keycode) {
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

/* A tap dance or combo emits press and release back to back for a TAP and
 * holds them apart for a HOLD, so the gap decides which one happened. */
static bool magic_release_is_tap(uint16_t down_ms) {
    uint32_t limit = (uint32_t)MAGIC_TAP_SLACK_MS + (uint32_t)QS_tap_code_delay;
    if (limit > MAGIC_TAP_LIMIT_MAX_MS) {
        limit = MAGIC_TAP_LIMIT_MAX_MS;
    }
    return timer_since(down_ms) <= (uint16_t)limit;
}

static bool is_layer_switch_keycode(uint16_t keycode) {
    return IS_QK_MOMENTARY(keycode) || IS_QK_TO(keycode) || IS_QK_TOGGLE_LAYER(keycode) ||
           IS_QK_LAYER_TAP_TOGGLE(keycode) || IS_QK_DEF_LAYER(keycode) || IS_QK_LAYER_MOD(keycode) ||
           IS_QK_ONE_SHOT_LAYER(keycode);
}

/* Keys that operate the mouse layer rather than use it. They never count as the
 * "next key" that spends a oneshot. Mouse1-8 and letters do. */
static bool is_mouse_control_keycode(uint16_t keycode) {
    return is_mouse_settings_key(keycode) || is_mod_lock_key(keycode) || is_wheel_keycode(keycode) ||
           is_layer_switch_keycode(keycode);
}

/* The mode latches differ between the two rules on purpose: pressing SCR or
 * T.GES *does* spend a pending O.SLO (only the O.* keys are exempt, so that
 * self-toggle works), but a tap dance whose tap is any mode latch must not. */
static bool should_cancel_mode_oneshot(uint16_t keycode) {
    return !skip_mode_oneshot_cancel && !is_oneshot_mode_key(keycode) && !is_mouse_control_keycode(keycode);
}

static bool is_oneshot_control_keycode(uint16_t keycode) {
    return is_mode_latch_key(keycode) || is_mouse_control_keycode(keycode);
}

/* TAP of O.SCR / SHOW / WH_* / OSL must not count as "next key".
 * TAP of Ctrl+C / Mouse1 never reaches process_record, so the TD press must. */
#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
/* Reads EEPROM on the first call for this event only; NULL if not a tap dance
 * or the entry could not be read. */
static const vial_tap_dance_entry_t *key_ctx_tap_dance(key_ctx_t *ctx) {
    if (!ctx->td_read) {
        ctx->td_read  = true;
        ctx->td_valid = IS_QK_TAP_DANCE(ctx->keycode) &&
                        dynamic_keymap_get_tap_dance((uint8_t)(ctx->keycode - QK_TAP_DANCE), &ctx->td) == 0;
    }
    return ctx->td_valid ? &ctx->td : NULL;
}
#endif

static bool tap_dance_is_oneshot_control(key_ctx_t *ctx) {
#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
    const vial_tap_dance_entry_t *td = key_ctx_tap_dance(ctx);
    if (td == NULL) {
        return false;
    }
    if (is_oneshot_control_keycode(td->on_tap) || is_oneshot_control_keycode(td->on_double_tap)) {
        return true;
    }
    return td->on_tap == KC_NO && is_oneshot_control_keycode(td->on_hold);
#else
    (void)ctx;
    return false;
#endif
}

static bool tap_dance_tap_is_mouse_button(key_ctx_t *ctx) {
#if defined(VIAL_ENABLE) && defined(TAP_DANCE_ENABLE)
    const vial_tap_dance_entry_t *td = key_ctx_tap_dance(ctx);
    return td != NULL && IS_MOUSEKEY_BUTTON(td->on_tap);
#else
    (void)ctx;
    return false;
#endif
}

static bool is_mouse_click_keycode(key_ctx_t *ctx) {
    return IS_MOUSEKEY_BUTTON(ctx->inner) || IS_MOUSEKEY_BUTTON(ctx->keycode) || tap_dance_tap_is_mouse_button(ctx);
}

/* Clicking the mouse and locking a button down are the same gesture as far as a
 * tap-latched D.SCR / T.GES / T.SLO / S.SCR is concerned: neither ends it.
 * Kept separate from is_mouse_click_keycode() because that one also decides
 * whether to spend a one-shot modifier, and a drag lock is consumed before the
 * button is actually held, which would drop the modifier mid-drag. */
static bool is_mouse_action_keycode(key_ctx_t *ctx) {
    return is_mouse_click_keycode(ctx) || is_drag_lock_key(ctx->keycode) || is_drag_lock_key(ctx->inner);
}

static bool should_cancel_from_event(key_ctx_t *ctx) {
    if (mode_cancel_event(ctx)) {
        return true;
    }
    return ctx->pressed && IS_QK_TAP_DANCE(ctx->keycode) && !tap_dance_is_oneshot_control(ctx);
}

/* QMK skips the OSL clear for modifier keycodes, so end it here. Run this after
 * process_action so store_or_get_action still sees the OSL layer. Consumed keys
 * (QK_KB, WH_*) return false and skip process_action, so those end OSL in
 * process_record_mouse. Wheel ticks still do not cancel O.SCR/O.GES/O.SLO. */
static bool should_end_oneshot_layer(key_ctx_t *ctx) {
    if (!should_cancel_from_event(ctx)) {
        return false;
    }
    return !is_layer_switch_keycode(ctx->inner) && !is_layer_switch_keycode(ctx->keycode) &&
           !tap_dance_is_oneshot_control(ctx);
}

static void fulfill_oneshot_layer(void) {
#ifndef NO_ACTION_ONESHOT
    /* clear_oneshot_layer_state() keeps the layer when OSL is still physically
     * held (MO-like) or tap-toggle locked, and releases it otherwise. Do not
     * force it off on top of that: with oneshot disabled in QMK Settings an OSL
     * is a plain momentary layer and never enters oneshot state at all. */
    clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED);
#endif
}

static void end_oneshots_for_key(key_ctx_t *ctx) {
    fulfill_oneshot_layer();
    if (is_mouse_click_keycode(ctx)) {
#ifndef NO_ACTION_ONESHOT
        clear_oneshot_mods();
#endif
    }
}

static void end_oneshots_if_consumed(key_ctx_t *ctx) {
    if (should_end_oneshot_layer(ctx)) {
        end_oneshots_for_key(ctx);
    }
}

/* Oneshot keys skip should_cancel so self-toggle still works. */
static void cancel_mode_oneshots(void) {
    for (uint8_t i = 0; i < MOUSE_MODE_COUNT; i++) {
        mode_state[i].oneshot = false;
    }
    clear_mode_fracs();
}

static void cancel_magic_holds(void) {
    /* MAGIC HOLD of SCR/GES/SLOW lasts while the tap-dance key is down, like a
     * physical hold. Do not drop those on another key. MAGIC TAP of those keys
     * becomes oneshot, which cancel_mode_oneshots handles.
     * Only the S.SCR tap latch ends here; a key still being held keeps it on
     * through its own holder count. */
    slow_scroll.latched = false;
    clear_mode_fracs();
}

static void cancel_magic_toggles(void) {
    for (uint8_t i = 0; i < MOUSE_MODE_COUNT; i++) {
        if (mode_state[i].toggle_magic) {
            mode_state[i].toggle       = false;
            mode_state[i].toggle_magic = false;
        }
    }
    clear_mode_fracs();
}

/* SCR / GES / SLOW: momentary while held. A tap dance TAP of the same key ends
 * as a oneshot instead; the gap between press and release decides which. */
static void mode_hold_key(mouse_mode_id_t id, bool pressed, bool magic) {
    mouse_mode_state_t *st       = &mode_state[id];
    bool                was_held = mode_held(st);

    if (!magic) {
        if (pressed) {
            if (st->hold_phys < UINT8_MAX) {
                st->hold_phys++;
            }
        } else if (st->hold_phys > 0) {
            st->hold_phys--;
        }
    } else if (pressed) {
        /* Counted even while the oneshot is armed: skipping the press would
         * leave its release to decrement someone else. The oneshot survives
         * anyway, since a hold never clears it and a tap re-arms it below. */
        if (st->hold_magic < UINT8_MAX) {
            if (st->hold_magic < MODE_MAGIC_HOLD_MAX) {
                st->hold_magic_ms[st->hold_magic] = timer_now();
            }
            st->hold_magic++;
        }
    } else if (st->hold_magic > 0) {
        st->hold_magic--;
        uint8_t slot = st->hold_magic < MODE_MAGIC_HOLD_MAX ? st->hold_magic : (uint8_t)(MODE_MAGIC_HOLD_MAX - 1);
        /* Only the last holder letting go can turn the hold into a oneshot. */
        if (st->hold_magic == 0 && magic_release_is_tap(st->hold_magic_ms[slot])) {
            /* mode_oneshot_enter() settles every mode and enters this one. */
            mode_oneshot_enter(id);
            return;
        }
    }

    if (mode_held(st)) {
        if (!was_held) {
            mode_enter(id);
        }
    } else {
        mode_settle(id);
    }
}

/* D.SCR / T.GES / T.SLO: flip on press. A tap dance TAP is undone by the next
 * non-mouse key (cancel_magic_toggles); a HOLD latches like a physical press,
 * and the TAP/HOLD decision is on release so a letter cannot drop it early. */
static void mode_toggle_key(mouse_mode_id_t id, bool pressed, bool magic) {
    mouse_mode_state_t *st = &mode_state[id];

    if (pressed) {
        st->toggle = !st->toggle;
        if (st->toggle) {
            mode_enter(id);
        }
        mode_settle(id);
    }
    if (!magic) {
        if (pressed) {
            st->toggle_magic = false;
        }
        return;
    }
    if (pressed) {
        st->toggle_down_ms = timer_now();
        st->toggle_magic   = false;
    } else {
        st->toggle_magic = st->toggle && magic_release_is_tap(st->toggle_down_ms);
    }
}

/* O.SCR / O.GES / O.SLO: arm until the next key, or disarm if already armed. */
static void mode_oneshot_key(mouse_mode_id_t id, bool pressed) {
    mouse_mode_state_t *st = &mode_state[id];

    if (!pressed) {
        return;
    }
    if (st->oneshot) {
        st->oneshot = false;
        mode_settle(id);
    } else {
        mode_oneshot_enter(id);
    }
}

/* S.SCR: momentary while any key holds it. A tap dance TAP additionally flips
 * a latch that lasts until the next key. Holders are counted so releasing one
 * of two keys does not cut the other short, and the latch is independent so a
 * HOLD taken from a latched state cannot swallow it. */
static void slow_scroll_key(bool pressed, bool magic) {
    if (!magic) {
        if (pressed) {
            if (slow_scroll.hold_phys < UINT8_MAX) {
                slow_scroll.hold_phys++;
            }
        } else if (slow_scroll.hold_phys > 0) {
            slow_scroll.hold_phys--;
        }
    } else if (pressed) {
        if (slow_scroll.hold_magic < UINT8_MAX) {
            if (slow_scroll.hold_magic < MODE_MAGIC_HOLD_MAX) {
                slow_scroll.hold_magic_ms[slow_scroll.hold_magic] = timer_now();
            }
            slow_scroll.hold_magic++;
        }
    } else if (slow_scroll.hold_magic > 0) {
        slow_scroll.hold_magic--;
        uint8_t slot = slow_scroll.hold_magic < MODE_MAGIC_HOLD_MAX ? slow_scroll.hold_magic
                                                                   : (uint8_t)(MODE_MAGIC_HOLD_MAX - 1);
        if (magic_release_is_tap(slow_scroll.hold_magic_ms[slot])) {
            slow_scroll.latched = !slow_scroll.latched;
        }
    }
}

static void wheel_repeat_reset(void); /* defined with the wheel code below */

static void mouse_modes_reset(void) {
    memset(mode_state, 0, sizeof(mode_state));
    memset(&slow_scroll, 0, sizeof(slow_scroll));
    skip_mode_oneshot_cancel = false;
    clear_drag_locks();
    clear_mod_locks();
    clear_slow_cursor_frac_if_idle();
    clear_gesture_motion();
    clear_cursor_recoil();
    wheel_repeat_reset();
    gesture_last_fired_ms = 0;
    wheel_move_v          = 0;
    wheel_move_h          = 0;
    wheel_batch_v         = 0;
    wheel_batch_h         = 0;
    scroll_out_reset();
    /* Drop leftover xy/h/v from this scan and release drag-lock buttons. */
    {
        report_mouse_t mouse = {0};
        pointing_device_set_report(mouse);
        mouse_send_flag = true;
    }
}

void mouse_config_save(void) {
    config_dirty                  = false;
    user_config.reserved          = 0;
    user_config.scroll_div        = ball_scroll_div;
    user_config.gesture_threshold = mouse_gesture_threshold;
    user_config.cursor_scale      = mouse_cursor_scale;
    user_config.reserved2         = 0;
    eeconfig_update_user(user_config.raw);
}

/* A Vial slider sends one set_value per step, so dragging 8 -> 80 used to be
 * ~70 EEPROM writes. VIA's own contract is set_value = RAM, id_custom_save =
 * persist; the same holds for the SCR+/DPI-/THR+ keycodes on key repeat. Mark
 * dirty instead and let mouse_config_task() write once things settle. Explicit
 * saves (id_custom_save, RST, OS override) still go straight through. */
static void mouse_config_touch(void) {
    config_dirty    = true;
    config_dirty_ms = timer_now();
}

static void mouse_config_task(void) {
    if (config_dirty && timer_since(config_dirty_ms) >= CONFIG_SAVE_DEBOUNCE_MS) {
        mouse_config_save();
    }
}

static uint8_t sanitize_u8(uint8_t value, uint8_t fallback, uint8_t lo, uint8_t hi) {
    if (value < lo || value > hi) {
        return fallback;
    }
    return value;
}

void mouse_config_reset(void) {
    mouse_modes_reset();
    ball_scroll_div         = USER_SCROLL_DIV_DEFAULT;
    mouse_gesture_threshold = USER_GESTURE_THRESHOLD_DEFAULT;
    mouse_cursor_scale      = USER_CURSOR_SCALE_DEFAULT;
    clear_cursor_scale_frac();
    mouse_config_save();
}

void mouse_config_load(void) {
    uint8_t div = sanitize_u8(user_config.scroll_div, USER_SCROLL_DIV_DEFAULT, SCROLL_DIV_MIN, SCROLL_DIV_MAX);
    uint8_t thr = sanitize_u8(user_config.gesture_threshold, USER_GESTURE_THRESHOLD_DEFAULT, GESTURE_THRESH_MIN, GESTURE_THRESH_MAX);
    uint8_t dpi = sanitize_u8(user_config.cursor_scale, USER_CURSOR_SCALE_DEFAULT, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX);

    ball_scroll_div         = div;
    mouse_gesture_threshold = thr;
    mouse_cursor_scale      = dpi;
    if (div != user_config.scroll_div || thr != user_config.gesture_threshold || dpi != user_config.cursor_scale) {
        mouse_config_save();
    }
}

static void apply_scroll_div(uint8_t value) {
    ball_scroll_div = clamp_u8(value, SCROLL_DIV_MIN, SCROLL_DIV_MAX);
}

static void apply_cursor_scale(uint8_t value) {
    mouse_cursor_scale = clamp_u8(value, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX);
    clear_cursor_scale_frac();
    clear_cursor_recoil();
}

static void apply_gesture_threshold(uint8_t value) {
    mouse_gesture_threshold = clamp_u8(value, GESTURE_THRESH_MIN, GESTURE_THRESH_MAX);
    clear_gesture_motion();
}

static uint8_t adjust_u8(uint8_t cur, int8_t dir, uint8_t step, uint8_t lo, uint8_t hi) {
    return clamp_u8((int16_t)cur + (dir > 0 ? step : -(int16_t)step), lo, hi);
}

static void adjust_ball_scroll_div(int8_t dir) {
    apply_scroll_div(adjust_u8(ball_scroll_div, dir, SCROLL_DIV_STEP, SCROLL_DIV_MIN, SCROLL_DIV_MAX));
    mouse_config_touch();
}

static void adjust_gesture_threshold(int8_t dir) {
    apply_gesture_threshold(adjust_u8(mouse_gesture_threshold, dir, GESTURE_THRESH_STEP, GESTURE_THRESH_MIN, GESTURE_THRESH_MAX));
    mouse_config_touch();
}

static void adjust_cursor_scale(int8_t dir) {
    apply_cursor_scale(adjust_u8(mouse_cursor_scale, dir, CURSOR_SCALE_STEP, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX));
    mouse_config_touch();
}

static uint8_t get_scroll_div(void) {
    return ball_scroll_div;
}

static uint8_t get_gesture_threshold(void) {
    return mouse_gesture_threshold;
}

static uint8_t get_cursor_scale(void) {
    return mouse_cursor_scale;
}

/* The VIA menu, the CLI and the keycodes all used to carry their own copy of
 * "which setter goes with which value", which is how one path ended up saving
 * to EEPROM while another did not. One table now feeds all three. */
typedef struct {
    uint8_t     value_id; /* enum via_mouse_value */
    const char *cli_name;
    const char *cli_alias; /* NULL when the command has no second spelling */
    void (*apply)(uint8_t value);
    uint8_t (*get)(void);
} mouse_value_t;

static const mouse_value_t mouse_values[] = {
    {id_mouse_scroll_div, "scroll", NULL, apply_scroll_div, get_scroll_div},
    {id_mouse_gesture_threshold, "thresh", "threshold", apply_gesture_threshold, get_gesture_threshold},
    {id_mouse_cursor_scale, "dpi", NULL, apply_cursor_scale, get_cursor_scale},
};

#define MOUSE_VALUE_COUNT ((uint8_t)(sizeof(mouse_values) / sizeof(mouse_values[0])))

static const mouse_value_t *mouse_value_by_id(uint8_t value_id) {
    for (uint8_t i = 0; i < MOUSE_VALUE_COUNT; i++) {
        if (mouse_values[i].value_id == value_id) {
            return &mouse_values[i];
        }
    }
    return NULL;
}

static const mouse_value_t *mouse_value_by_name(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (uint8_t i = 0; i < MOUSE_VALUE_COUNT; i++) {
        const mouse_value_t *v = &mouse_values[i];
        if (strcmp(name, v->cli_name) == 0 || (v->cli_alias != NULL && strcmp(name, v->cli_alias) == 0)) {
            return v;
        }
    }
    return NULL;
}

/* The sign is spelled out per keycode because it is not self-evident: a smaller
 * scroll divisor is faster and a lower gesture threshold fires more easily, but
 * a larger cursor scale is faster. */
static const struct {
    uint16_t kc;
    int8_t   dir;
    void (*adjust)(int8_t dir);
} mouse_adjusters[] = {
    {U_SCR_FASTER, -1, adjust_ball_scroll_div}, {U_SCR_SLOWER, +1, adjust_ball_scroll_div},
    {U_THR_EASIER, -1, adjust_gesture_threshold}, {U_THR_HARDER, +1, adjust_gesture_threshold},
    {U_DPI_FASTER, +1, adjust_cursor_scale},    {U_DPI_SLOWER, -1, adjust_cursor_scale},
};

#define MOUSE_ADJUSTER_COUNT ((uint8_t)(sizeof(mouse_adjusters) / sizeof(mouse_adjusters[0])))

static bool process_mouse_adjuster(uint16_t keycode, bool pressed) {
    for (uint8_t i = 0; i < MOUSE_ADJUSTER_COUNT; i++) {
        if (mouse_adjusters[i].kc == keycode) {
            if (pressed) {
                mouse_adjusters[i].adjust(mouse_adjusters[i].dir);
            }
            return true;
        }
    }
    return false;
}

static void send_uint_decimal(unsigned v) {
    char    buf[4];
    uint8_t i = 0;
    if (v >= 100) {
        buf[i++] = (char)('0' + (v / 100));
        v %= 100;
        buf[i++] = (char)('0' + (v / 10));
        v %= 10;
    } else if (v >= 10) {
        buf[i++] = (char)('0' + (v / 10));
        v %= 10;
    }
    buf[i++] = (char)('0' + v);
    buf[i]   = '\0';
    send_string(buf);
}

static void send_mouse_settings(void) {
    if (ball_is_scroll()) {
        SEND_STRING("s ");
    } else if (ball_is_gesture()) {
        SEND_STRING("g ");
    } else {
        SEND_STRING("c ");
    }
    SEND_STRING("s");
    send_uint_decimal((unsigned)ball_scroll_div);
    SEND_STRING(" t");
    send_uint_decimal((unsigned)mouse_gesture_threshold);
    SEND_STRING(" d");
    send_uint_decimal((unsigned)mouse_cursor_scale);
    SEND_STRING("\n");
}

static uint8_t active_layer(void) {
    return get_highest_layer(layer_state | default_layer_state);
}

static uint8_t gesture_action_layer(void) {
    uint8_t layer = active_layer();
    return layer == 0 ? GESTURE_ACTION_LAYER : layer;
}

static uint16_t keymap_identity_to_keycode(uint8_t layer, uint16_t identity) {
    return dynamic_keymap_get_keycode(layer, identity_row(identity), identity_col(identity));
}

static bool is_action_keycode(uint16_t keycode) {
    return keycode != KC_NO && keycode != KC_TRNS;
}

static bool slot_has_user_key(uint8_t layer, uint16_t identity) {
    if (layer == 0) {
        return false;
    }
    return is_action_keycode(keymap_identity_to_keycode(layer, identity));
}

/* Gesture mode is the default on a layer that maps the orange slots but leaves
 * the four ball-direction cells alone: there the ball has no other job. */
static bool compute_gesture_default_from_keymap(uint8_t layer) {
    static const uint16_t ball_usages[] = {KC_MS_UP, KC_MS_DOWN, KC_MS_LEFT, KC_MS_RIGHT};
    bool                  gesture_mapped = false;

    if (layer == 0) {
        return false;
    }
    for (uint8_t i = 0; i < GESTURE_SLOT_COUNT; i++) {
        if (slot_has_user_key(layer, gesture_slot(i))) {
            gesture_mapped = true;
            break;
        }
    }
    if (!gesture_mapped) {
        return false;
    }
    for (uint8_t i = 0; i < (uint8_t)(sizeof(ball_usages) / sizeof(ball_usages[0])); i++) {
        if (slot_has_user_key(layer, ball_usages[i])) {
            return false;
        }
    }
    return true;
}

static bool gesture_default_from_keymap(void) {
    return compute_gesture_default_from_keymap(active_layer());
}

static gesture_id_t recognize_gesture(int32_t x, int32_t y) {
    int32_t ax = abs32(x);
    int32_t ay = abs32(y);
    if (ax + ay < (int32_t)mouse_gesture_threshold) {
        return GESTURE_NONE;
    }
    if (ax >= ay) {
        return x >= 0 ? GESTURE_RIGHT : GESTURE_LEFT;
    }
    return y < 0 ? GESTURE_UP : GESTURE_DOWN;
}

static uint8_t virtual_exec_depth;

/* Same path as a Vial remap on a real button: MAGIC action_exec, not
 * register_code16 (TAP dance TAP), so LCTL(kc) stays on stock QMK. */
static void tap_mapped_keycode(uint16_t keycode) {
    if (virtual_exec_depth != 0) {
        return;
    }
    virtual_exec_depth++;
    uint16_t saved = g_vial_magic_keycode_override;
    g_vial_magic_keycode_override = keycode;
    action_exec((keyevent_t){.type    = KEY_EVENT,
                             .key     = (keypos_t){.row = VIAL_MATRIX_MAGIC, .col = VIAL_MATRIX_MAGIC},
                             .pressed = true,
                             .time    = timer_now()});
    qs_wait_ms(QS_tap_code_delay);
    action_exec((keyevent_t){.type    = KEY_EVENT,
                             .key     = (keypos_t){.row = VIAL_MATRIX_MAGIC, .col = VIAL_MATRIX_MAGIC},
                             .pressed = false,
                             .time    = timer_now()});
    g_vial_magic_keycode_override = saved;
    virtual_exec_depth--;
}

static void process_gesture(uint8_t layer, gesture_id_t gesture_id) {
    if (gesture_id < GESTURE_RIGHT || gesture_id > GESTURE_DOWN) {
        return;
    }
    uint16_t keycode = keymap_identity_to_keycode(layer, gesture_slot((uint8_t)(gesture_id - GESTURE_RIGHT)));
    if (is_action_keycode(keycode)) {
        tap_mapped_keycode(keycode);
    }
}

typedef struct {
    mouse_xy_report_t x;
    mouse_xy_report_t y;
} scaled_report_t;

static void take_scaled(int32_t real, int32_t div, int32_t *full, int16_t *frac) {
    if (div < 1) {
        div = 1;
    }
    int32_t q = real / div;
    *frac     = (int16_t)(real - q * div);
    *full     = q;
}

static void calc_mouse_scaled_move(mouse_parse_result_t const *report, scaled_report_t *scaled) {
    int32_t x_full, y_full;

    take_scaled((int32_t)report->x * mouse_cursor_scale + cursor_scale_x_frac, CURSOR_SCALE_UNIT, &x_full, &cursor_scale_x_frac);
    take_scaled((int32_t)report->y * mouse_cursor_scale + cursor_scale_y_frac, CURSOR_SCALE_UNIT, &y_full, &cursor_scale_y_frac);

    scaled->x = clamp_xy(x_full);
    scaled->y = clamp_xy(y_full);
}

static int32_t reject_cursor_recoil(int32_t now, int32_t prev) {
    if (abs32(prev) >= CURSOR_RECOIL_PREV && abs32(now) <= CURSOR_RECOIL_MAX && (now > 0) != (prev > 0)) {
        return 0;
    }
    return now;
}

static int32_t recoil_axis(int32_t now, int32_t *prev) {
    int32_t out = now;
    if (now != 0) {
        out   = reject_cursor_recoil(now, *prev);
        *prev = now;
    } else {
        *prev = 0;
    }
    return out;
}

static mouse_xy_report_t apply_slow_cursor(int32_t delta, int16_t *frac) {
    if (!cursor_is_slow()) {
        return clamp_xy(delta);
    }
    int32_t out;
    take_scaled(delta + *frac, SLOW_CURSOR_DIV, &out, frac);
    return clamp_xy(out);
}

static int32_t scroll_div(void) {
    int32_t div = ball_scroll_div;
    if (slow_scroll_is_active()) {
        div *= SLOW_SCROLL_MUL;
    }
    return div < 1 ? 1 : div;
}

/* Sensor: X+ right, Y+ down. Mapping: h = x, v = -y (ball-up = wheel-up).
 * scroll_div() raw ball counts make one detent. */
static void apply_ball_scroll(report_mouse_t *mouse, int32_t x, int32_t y) {
    mouse->x = 0;
    mouse->y = 0;
    mouse->h = 0;
    mouse->v = 0;
    scroll_out_ball(x, -y, scroll_div());
    mouse_send_flag = true;
}

static void apply_ball_cursor(report_mouse_t *mouse, scaled_report_t const *scaled) {
    /* All wheel output waits in scroll_out and is written into the report
     * by mouse_wheel_flush() at send time, so h/v here are stale. */
    mouse->h = 0;
    mouse->v = 0;
    /* Wheel/pan-only packets have x=y=0. Do not treat that as "cursor stopped"
     * or recoil forgets the last ball motion and a reverse tick leaks through. */
    if (scaled->x == 0 && scaled->y == 0) {
        return;
    }

    int32_t gx = recoil_axis(scaled->x, &cursor_prev_x);
    int32_t gy = recoil_axis(scaled->y, &cursor_prev_y);
    if (gx != 0) {
        mouse_xy_report_t ox = apply_slow_cursor(gx, &slow_cursor_x_frac);
        if (ox != 0) {
            mouse_send_flag = true;
            mouse->x        = clamp_xy((int32_t)mouse->x + ox);
        }
    }
    if (gy != 0) {
        mouse_xy_report_t oy = apply_slow_cursor(gy, &slow_cursor_y_frac);
        if (oy != 0) {
            mouse_send_flag = true;
            mouse->y        = clamp_xy((int32_t)mouse->y + oy);
        }
    }
}

static void apply_ball_gesture(int32_t x, int32_t y) {
    if (x != 0 || y != 0) {
        gesture_idle_ms = timer_now();
        gesture_move_x += x;
        gesture_move_y += y;
    }

    gesture_id_t gesture_id = recognize_gesture(gesture_move_x, gesture_move_y);
    if (gesture_id == GESTURE_NONE) {
        return;
    }
    if (gesture_dir_lock == GESTURE_NONE) {
        gesture_dir_lock = gesture_id;
    }
    if (gesture_id == gesture_dir_lock && timer_since(gesture_last_fired_ms) > GESTURE_COOLDOWN_MS) {
        bool saved = skip_mode_oneshot_cancel;
        skip_mode_oneshot_cancel = true;
        if (mode_active(MODE_GESTURE)) {
            process_gesture(gesture_action_layer(), gesture_id);
        } else {
            process_gesture(active_layer(), gesture_id);
        }
        skip_mode_oneshot_cancel = saved;
        gesture_last_fired_ms    = timer_now();
        dprintf("Gesture fired: id:%d x:%d,y:%d\n", (int)gesture_id, (int)gesture_move_x, (int)gesture_move_y);
    }
    gesture_move_x = 0;
    gesture_move_y = 0;
}

/* Shared mouse EP: last USB report wins. Physical clicks live in mousekey;
 * drag locks are firmware flags. Rebuild buttons at every send. */
void mouse_merge_buttons(report_mouse_t *mouse) {
    uint8_t buttons = 0;
    if (mouse == NULL) {
        return;
    }
#ifdef MOUSEKEY_ENABLE
    buttons = mousekey_get_report().buttons;
#endif
    for (uint8_t i = 0; i < DRAG_LOCK_COUNT; i++) {
        if (drag_locks[i].locked) {
            buttons |= drag_locks[i].hid_button;
        }
    }
    mouse->buttons = buttons;
}

void pointing_device_keycode_handler(uint16_t keycode, bool pressed) {
    if (!IS_MOUSEKEY_BUTTON(keycode)) {
        return;
    }
    if (pressed) {
        drag_lock_t *entry = drag_lock_for_mouse(keycode);
        if (entry != NULL) {
            entry->locked = false;
        }
    }
    report_mouse_t mouse = pointing_device_get_report();
    mouse_merge_buttons(&mouse);
    mouse_wheel_flush(&mouse);
    pointing_device_set_report(mouse);
    pointing_device_send();
    /* Motion in this report is on the wire. Do not send it again; only wheel
     * output that did not fit is left for the next report. */
    mouse_send_flag = false;
    mouse_after_send();
}

/* Buttons the host has: what the regular send (pointing_device_send) last
 * delivered. The direct sends below repeat them unchanged. */
static uint8_t last_sent_buttons = 0;

static void wait_mouse_endpoint_idle(void) {
#ifdef MOUSE_WHEEL_RESOLUTION_MULTIPLIER
    extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];
    uint16_t                 start = timer_read();
    while (!usb_endpoint_in_is_inactive(&usb_endpoints_in[USB_ENDPOINT_IN_MOUSE]) && timer_elapsed(start) < 10) {
    }
#endif
}

/* Sends the urgent wheel output (it came with a modifier) now, report after
 * report, while the modifier is still down; a remainder carried to a later
 * report could arrive after it is released. 128 reports cover the largest
 * value one input can carry (127 detents x 120 counts / 127 per report).
 *
 * Wheel only: ordinary scrolling and cursor motion stay queued for the
 * regular send. This can run inside the matrix scan, before QMK applies a
 * button change from the same device report, so it must not send motion
 * with the old buttons. The buttons it repeats are the ones the regular send
 * last delivered, so a drag-lock change still waiting for that send is
 * neither sent early nor skipped by it later.
 *
 * It then waits (up to 10 ms) until the mouse endpoint has handed the reports
 * to the host, so a modifier release queued right after cannot overtake them
 * on the keyboard endpoint. The host's own processing order across endpoints
 * is outside the device's control. */
static void send_wheel_now(void) {
    bool sent = false;
    for (uint8_t i = 0; i < 128 && scroll_out_urgent_pending(); i++) {
        report_mouse_t wheel = {.buttons = last_sent_buttons};
        scroll_out_flush_urgent(&wheel);
        host_mouse_send(&wheel);
        sent = true;
    }
    if (sent) {
        wait_mouse_endpoint_idle();
    }
    if (scroll_out_pending()) {
        mouse_send_flag = true;
    }
}

/* patches/0003: QMK's own mousekey reports land here instead of going straight
 * to the host. That covers what process_wheel_keycode() never sees: WH_* with
 * a modifier (LCTL(WH_UP)), macros, and MS_* keys. The wheel gets the host
 * multiplier like every other wheel source, and the report keeps the drag-lock
 * buttons. Sent right away, because a modified key releases its modifier just
 * after this returns.
 *
 * Only this report's own motion and wheel go out here. Ordinary scrolling and
 * ball motion waiting for the regular send stay where they are: this can run
 * with a modifier down, and inside the matrix scan before a button change
 * from the same device report is applied. */
void mousekey_host_send(report_mouse_t *report) {
    if (report == NULL) {
        return;
    }
    scroll_out_detents(report->v, true, true);
    scroll_out_detents(report->h, false, true);
    report_mouse_t mouse = {.buttons = last_sent_buttons, .x = report->x, .y = report->y};
    scroll_out_flush_urgent(&mouse);
    host_mouse_send(&mouse);
    send_wheel_now();
}

static void exec_identity_tap(uint16_t kc) {
    if (virtual_exec_depth != 0) {
        return;
    }
    keypos_t key = {.row = identity_row(kc), .col = identity_col(kc)};
    uint16_t t   = timer_now();
    virtual_exec_depth++;
    is_encoder_action = true;
    action_exec((keyevent_t){.key = key, .type = KEY_EVENT, .pressed = true, .time = t});
    action_exec((keyevent_t){.key = key, .type = KEY_EVENT, .pressed = false, .time = t});
    is_encoder_action = false;
    virtual_exec_depth--;
}

static int16_t encoder_analog_move(void) {
    /* Consume on read so identity-tap release does not emit again, but a
     * later WH_LEFT tap in the same scan can still use wheel_move_h. */
    if (wheel_move_v != 0) {
        int16_t move = wheel_move_v;
        wheel_move_v = 0;
        return move;
    }
    int16_t move = wheel_move_h;
    wheel_move_h = 0;
    return move;
}

/* QMK and HID agree that WH_UP / WH_RIGHT are the positive directions of
 * report.v / report.h (see quantum/mousekey.c). wheel_kc_is_positive() states
 * that, and a key bound to WH_* emits accordingly.
 *
 * wheel_kc_for_delta() answers a different question: which Vial slot a physical
 * wheel movement should fire. That depends on the device, not on the standard.
 * The ELECOM HUGE PLUS reports AC Pan with the opposite sign (a tilt to the
 * left sends a positive value, measured with the "hid" CLI command), so its
 * horizontal case is mirrored. The scroll direction is unaffected either way:
 * the physical path passes the device value through untouched. */
static bool wheel_kc_is_vertical(uint16_t keycode) {
    return keycode == KC_MS_WH_UP || keycode == KC_MS_WH_DOWN;
}

static bool wheel_kc_is_positive(uint16_t keycode) {
    return keycode == KC_MS_WH_UP || keycode == KC_MS_WH_RIGHT;
}

static uint16_t wheel_kc_for_delta(int32_t delta, bool vertical) {
    if (vertical) {
        return delta > 0 ? KC_MS_WH_UP : KC_MS_WH_DOWN;
    }
    /* Device quirk, not the HID convention: positive AC Pan means tilted left. */
    return delta > 0 ? KC_MS_WH_LEFT : KC_MS_WH_RIGHT;
}

/* Adds val detents from a wheel or WH_* key. Everything about direction is
 * already baked into val, so this never consults a keycode. */
static void add_wheel(int16_t val, bool vertical) {
    if (val == 0) {
        return;
    }
    /* With a modifier down (Ctrl + wheel = zoom), the scroll must reach the
     * host while the modifier still is: send it now, report after report.
     * Only modifiers this keyboard sends are visible here. */
    uint8_t mods = get_mods() | get_weak_mods();
#ifndef NO_ACTION_ONESHOT
    mods |= get_oneshot_mods();
#endif
    scroll_out_detents(clamp_hid8(val), vertical, mods != 0);
    if (mods != 0) {
        send_wheel_now();
    } else {
        mouse_send_flag = true;
    }
}

/* A key bound to WH_* emits the direction QMK assigns to that keycode. */
static void emit_analog_wheel(int16_t move, bool vertical, bool positive_key) {
    if (move == 0) {
        /* Identity-tap release: analog already queued on press. */
        return;
    }
    int8_t mag = clamp_hid8(abs32(move));
    add_wheel(positive_key ? mag : (int16_t)(-mag), vertical);
}

/* Consuming WH_* keeps drag locks alive but also bypasses mousekey_on(), which
 * is where QMK's wheel auto-repeat lives. Run the same repeat here off the same
 * QMK Settings values mousekey_task() uses, so the Vial sliders still apply.
 * One wheel key repeats at a time; a new press takes over. */
/* The slot mouse_scan_end() routed the current batch to. The batch sign and
 * this slot always agree, so comparing the slot against the keycode that came
 * back tells remapping apart from routing: only a user remap to the opposite
 * direction should invert the scroll. */
static uint16_t wheel_routed_kc = KC_NO;

static uint16_t wheel_repeat_kc      = KC_NO;
static uint16_t wheel_repeat_ms      = 0;
static bool     wheel_repeat_started = false;

static void wheel_repeat_reset(void) {
    wheel_repeat_kc = KC_NO;
}

static void wheel_repeat_stop(uint16_t keycode) {
    if (wheel_repeat_kc == keycode) {
        wheel_repeat_reset();
    }
}

static void wheel_repeat_start(uint16_t keycode) {
    wheel_repeat_kc      = keycode;
    wheel_repeat_ms      = timer_now();
    wheel_repeat_started = false;
}

/* Called from mouse_scan_end(), once per scan, after the physical wheel. */
static void wheel_repeat_task(void) {
#ifdef MOUSEKEY_ENABLE
    if (wheel_repeat_kc == KC_NO) {
        return;
    }
    uint16_t wait = wheel_repeat_started ? (uint16_t)mk_wheel_interval : (uint16_t)(mk_wheel_delay * 10);
    if (timer_since(wheel_repeat_ms) <= wait) {
        return;
    }
    wheel_repeat_started = true;
    wheel_repeat_ms      = timer_now();
    emit_analog_wheel(1, wheel_kc_is_vertical(wheel_repeat_kc), wheel_kc_is_positive(wheel_repeat_kc));
#endif
}

static void process_wheel_keycode(uint16_t keycode, bool pressed) {
    bool vertical = wheel_kc_is_vertical(keycode);
    bool positive = wheel_kc_is_positive(keycode);

    if (is_encoder_action) {
        /* Synthetic tap from a physical wheel: the batch already carries the
         * device direction, so routing it to a slot must not change it. Only a
         * keymap that points that slot at the opposite direction flips it,
         * which is how the scroll direction is inverted from Vial. Never
         * repeats: the wheel sends its own events. */
        int16_t move = encoder_analog_move();
        if (wheel_routed_kc == KC_NO || keycode == wheel_routed_kc) {
            /* Identity slot: the device value already points the right way. */
            add_wheel(move, vertical);
        } else if (vertical == wheel_kc_is_vertical(wheel_routed_kc)) {
            /* Remapped inside the same axis. The slot already matched the
             * physical direction, so the only thing the remap can mean is
             * "invert this axis". */
            add_wheel(positive == wheel_kc_is_positive(wheel_routed_kc) ? move : (int16_t)-move, vertical);
        } else {
            /* Remapped onto the other axis: the destination keycode alone
             * decides which axis and which way. */
            emit_analog_wheel(move, vertical, positive);
        }
        return;
    }
    /* WH_* is handled here, repeat included; falling through to mousekey
     * as well would scroll twice. */
    if (pressed) {
        emit_analog_wheel(1, vertical, positive);
        wheel_repeat_start(keycode);
    } else {
        wheel_repeat_stop(keycode);
    }
}

static void map_buttons_to_matrix(uint8_t button_low) {
    /* Bits 8-15 must not be mapped: KC_MS_BTN1+8 collides with wheel keycodes. */
    hid_stats.last_button = button_low;
    if (matrix_dest == NULL) {
        return;
    }
    for (uint8_t bit = 0; bit < 8; bit++) {
        uint16_t kc   = (uint16_t)(KC_MS_BTN1 + bit);
        matrix_row_t m = (matrix_row_t)1 << identity_col(kc);
        if (button_low & (1u << bit)) {
            matrix_dest[identity_row(kc)] |= m;
        } else {
            matrix_dest[identity_row(kc)] &= ~m;
        }
    }
}

void keyboard_report_post_hook(void) {
    hid_stats.kb_count++;
    if (matrix_dest != NULL) {
        map_buttons_to_matrix(hid_stats.last_button);
    }
}

void mouse_on_host_disconnect(void) {
    hid_stats.last_undefined = 0;
    mouse_modes_reset();
    map_buttons_to_matrix(0);
    clear_cursor_scale_frac();
}

void hid_note_consumer(uint16_t report) {
    hid_stats.consumer_count++;
    hid_stats.last_consumer = report;
}

void hid_note_system(uint16_t report) {
    hid_stats.system_count++;
    hid_stats.last_system = report;
}

void hid_note_vendor(uint16_t usage_page) {
    hid_stats.vendor_count++;
    hid_stats.last_vendor = usage_page;
}

void mouse_report_hook(mouse_parse_result_t const *report) {
    if (report == NULL) {
        return;
    }
    hid_stats.mouse_count++;
    hid_stats.last_undefined = report->undefined;
    /* Wheel/pan-only collections have no button fields. Do not treat that as
     * all buttons released, or a click-and-drag dies when the wheel moves. */
    if (report->has_button) {
        map_buttons_to_matrix((uint8_t)(report->button & 0x00FF));
    }

    /* Physical wheel/tilt are queued and merged into the same USB report as
     * ball motion (cursor, gesture, or ball-scroll). */
    if (report->v != 0) {
        wheel_batch_v       += report->v;
        hid_stats.last_wheel = report->v;
    }
    if (report->h != 0) {
        wheel_batch_h     += report->h;
        hid_stats.last_pan = report->h;
    }

    report_mouse_t mouse = pointing_device_get_report();

    if (ball_is_scroll()) {
        clear_cursor_recoil();
        /* Ball counts are scroll, not a later flick. */
        clear_gesture_motion();
        apply_ball_scroll(&mouse, report->x, report->y);
        pointing_device_set_report(mouse);
        return;
    }

    if (ball_is_gesture()) {
        /* Ball counts drive keymap actions instead of motion, so commit the
         * cleared report before running them. A gesture can be bound to any
         * keycode, including ones that write the pointing device report
         * themselves (a click, a WH_* key, RST), and writing back a snapshot
         * taken beforehand would silently undo whatever they did. */
        mouse.x = 0;
        mouse.y = 0;
        mouse.h = 0;
        mouse.v = 0;
        pointing_device_set_report(mouse);
        clear_cursor_recoil();
        apply_ball_gesture(report->x, report->y);
        return;
    }

    clear_gesture_motion();
    scaled_report_t scaled;
    calc_mouse_scaled_move(report, &scaled);
    apply_ball_cursor(&mouse, &scaled);
    pointing_device_set_report(mouse);
}

void mouse_scan_end(void) {
    if (wheel_batch_v != 0) {
        wheel_move_v    = (int16_t)clamp_i32(wheel_batch_v, XY_REPORT_MIN, XY_REPORT_MAX);
        wheel_routed_kc = wheel_kc_for_delta(wheel_batch_v, true);
        exec_identity_tap(wheel_routed_kc);
        wheel_move_v    = 0;
        wheel_routed_kc = KC_NO;
    }
    if (wheel_batch_h != 0) {
        wheel_move_h    = (int16_t)clamp_i32(wheel_batch_h, XY_REPORT_MIN, XY_REPORT_MAX);
        wheel_routed_kc = wheel_kc_for_delta(wheel_batch_h, false);
        exec_identity_tap(wheel_routed_kc);
        wheel_move_h    = 0;
        wheel_routed_kc = KC_NO;
    }
    wheel_batch_v = 0;
    wheel_batch_h = 0;
    /* After the physical wheel taps, so a held WH_* key and the wheel merge
     * into the same report instead of overwriting each other. */
    wheel_repeat_task();
}

/* Moves up to ±127 host counts of pending wheel output into the report. */
void mouse_wheel_flush(report_mouse_t *mouse) {
    scroll_out_flush(mouse);
}

void mouse_after_send(void) {
    /* pointing_device_send() keeps the buttons in the report after sending. */
    last_sent_buttons = pointing_device_get_report().buttons;
    /* Whatever did not fit goes out with the next report. */
    if (scroll_out_pending()) {
        mouse_send_flag = true;
    }
}

bool process_record_mouse(uint16_t keycode, keyrecord_t *record) {
    if (record == NULL) {
        return true;
    }

    key_ctx_t ctx;
    key_ctx_init(&ctx, keycode, record);
    const bool pressed = ctx.pressed;
    const bool magic   = ctx.magic;

    if (should_cancel_from_event(&ctx)) {
        if (should_cancel_mode_oneshot(ctx.inner)) {
            cancel_mode_oneshots();
            /* MAGIC TAP of D.SCR/T.GES/T.SLO/S.SCR stays through mouse actions. */
            if (!is_mode_latch_key(ctx.inner) && !is_mode_latch_key(keycode) && !is_mouse_action_keycode(&ctx)) {
                cancel_magic_holds();
                cancel_magic_toggles();
            }
        }
        /* Consumed keys never reach post_process_record. Pass-through keys
         * (LCTL(kc), letters, mods) wait until after process_action. */
        if (IS_QK_KB(keycode)) {
            end_oneshots_if_consumed(&ctx);
        }
    }

    if (process_drag_lock_keycode(keycode, pressed) || process_mod_lock_keycode(keycode, pressed)) {
        return false;
    }
    note_physical_mod(keycode, pressed);

    /* SCR/GES/SLOW, D.SCR/T.GES/T.SLO and O.SCR/O.GES/O.SLO all run the same
     * three handlers over the mode_defs[] table. */
    mouse_mode_id_t mode = mode_for_key(keycode);
    if (mode != MOUSE_MODE_COUNT) {
        if (keycode == mode_defs[mode].kc_hold) {
            mode_hold_key(mode, pressed, magic);
        } else if (keycode == mode_defs[mode].kc_toggle) {
            mode_toggle_key(mode, pressed, magic);
        } else {
            mode_oneshot_key(mode, pressed);
        }
        return false;
    }
    if (keycode == U_SSCR) {
        slow_scroll_key(pressed, magic);
        return false;
    }

    if (process_mouse_adjuster(keycode, pressed)) {
        return false;
    }

    switch (keycode) {
        case U_SHOW:
            if (pressed) {
                bool saved = skip_mode_oneshot_cancel;
                skip_mode_oneshot_cancel = true;
                send_mouse_settings();
                skip_mode_oneshot_cancel = saved;
            }
            return false;
        case U_RST:
            if (pressed) {
                mouse_config_reset();
            }
            return false;
        case U_CLR:
            if (pressed) {
                mouse_modes_reset();
            }
            return false;
        case KC_MS_WH_UP ... KC_MS_WH_RIGHT:
            process_wheel_keycode(keycode, pressed);
            end_oneshots_if_consumed(&ctx);
            return false;
        default:
            /* Do not let an unhandled QK_KB_* fall through to process_action. */
            return !IS_QK_KB(keycode);
    }
}

void post_process_record_mouse(uint16_t keycode, keyrecord_t *record) {
    if (record == NULL || IS_QK_KB(keycode) || is_wheel_keycode(keycode)) {
        return;
    }
    key_ctx_t ctx;
    key_ctx_init(&ctx, keycode, record);
    if (should_end_oneshot_layer(&ctx)) {
        end_oneshots_for_key(&ctx);
    }
}

void mouse_housekeeping(void) {
    apply_mod_locks();
    mouse_config_task();
    /* The host may switch the wheel multiplier at any time, which can make a
     * fraction that was waiting sendable. */
    if (scroll_out_pending()) {
        mouse_send_flag = true;
    }
    if (gesture_idle_ms != 0) {
        uint16_t idle = timer_since(gesture_idle_ms);
        if (gesture_dir_lock != GESTURE_NONE && idle > GESTURE_LOCK_IDLE_MS) {
            clear_gesture_motion();
        } else if (idle > GESTURE_ACCUM_IDLE_MS) {
            gesture_move_x  = 0;
            gesture_move_y  = 0;
            gesture_idle_ms = 0;
        }
    }
}

void mouse_cli_print(void) {
    const char *mode = "c";
    if (ball_is_scroll()) {
        mode = "s";
    } else if (ball_is_gesture()) {
        mode = "g";
    }
    printf("mode %s  s%u t%u d%u  slow %u  lock L%u M%u R%u  mod C%u S%u A%u W%u\n", mode, (unsigned)ball_scroll_div,
           (unsigned)mouse_gesture_threshold, (unsigned)mouse_cursor_scale, (unsigned)cursor_is_slow(),
           (unsigned)drag_locks[0].locked, (unsigned)drag_locks[1].locked, (unsigned)drag_locks[2].locked,
           (unsigned)mod_locks[0].locked, (unsigned)mod_locks[1].locked, (unsigned)mod_locks[2].locked,
           (unsigned)mod_locks[3].locked);
}

bool mouse_set_named_value(const char *name, uint8_t value) {
    const mouse_value_t *v = mouse_value_by_name(name);
    if (v == NULL) {
        return false;
    }
    v->apply(value);
    mouse_config_touch();
    return true;
}

static void mouse_via_set_value(uint8_t *data) {
    if (data[0] == id_mouse_settings_reset) {
        mouse_config_reset();
        return;
    }
    const mouse_value_t *v = mouse_value_by_id(data[0]);
    if (v != NULL) {
        v->apply(data[1]);
        mouse_config_touch();
    }
}

static void mouse_via_get_value(uint8_t *data) {
    const mouse_value_t *v = mouse_value_by_id(data[0]);
    data[1]                = (v != NULL) ? v->get() : 0;
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (data == NULL || length < 2) {
        return;
    }

    uint8_t *command_id        = &(data[0]);
    uint8_t *channel_id        = &(data[1]);
    uint8_t *value_id_and_data = &(data[2]);

    if (*channel_id != id_custom_channel) {
        *command_id = id_unhandled;
        return;
    }

    switch (*command_id) {
        case id_custom_set_value:
            if (length >= 4) {
                mouse_via_set_value(value_id_and_data);
            }
            break;
        case id_custom_get_value:
            if (length >= 4) {
                mouse_via_get_value(value_id_and_data);
            }
            break;
        case id_custom_save:
            mouse_config_save();
            break;
        default:
            *command_id = id_unhandled;
            break;
    }
}

void hid_cli_print(void) {
    printf("kb %u  mouse %u  consumer %u last=0x%04X  system %u last=0x%04X\n", (unsigned)hid_stats.kb_count,
           (unsigned)hid_stats.mouse_count, (unsigned)hid_stats.consumer_count, (unsigned)hid_stats.last_consumer,
           (unsigned)hid_stats.system_count, (unsigned)hid_stats.last_system);
    printf("vendor %u last=0x%04X  buttons 0x%02X  undefined 0x%04X\n", (unsigned)hid_stats.vendor_count,
           (unsigned)hid_stats.last_vendor, (unsigned)hid_stats.last_button, (unsigned)hid_stats.last_undefined);
    /* HID and QMK both mean "up" and "right" by a positive value, so a tilt to
     * the right must show pan>0 here. If it does not, the device inverts AC Pan
     * and wheel_kc_for_delta() is where that is corrected. */
    printf("wheel %d  pan %d  (positive = up / right)\n", (int)hid_stats.last_wheel, (int)hid_stats.last_pan);
    /* What the PC asked for: x1 until Windows/Linux enable high resolution. */
    printf("host wheel x%d  pan x%d\n", (int)wheel_multiplier(true), (int)wheel_multiplier(false));
}

void hid_cli_print_desc(void) {
    print_hid_devices_cli();
}
