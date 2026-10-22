/* Host tests for the mouse key rules (keymaps/vial/mouse_keys.c with the
 * real modes.c): which key events end O.SCR / O.GES / O.SLO, tap-latched
 * toggles and the S.SCR latch, and when QMK's own oneshots are ended. */

#include <string.h>

#include "check.h"
#include "qmk_stub.h"
#include "dynamic_keymap.h"
#include "vial.h"

#include "kq_keycodes.h"
#include "modes.h"
#include "mouse_keys.h"
#include "settings.h"

/* ---- stand-ins for the rest of the keymap ---- */

static bool injecting;
static int  osl_cleared, osm_cleared, steps;

bool mouse_injecting(void) {
    return injecting;
}
void mouse_print_settings_string(void) {}
void mouse_reset_latches(void) {
    modes_reset();
}
void mouse_reset_settings_and_latches(void) {
    modes_reset();
}
void settings_step(setting_t s, int8_t dir) {
    (void)s;
    (void)dir;
    steps++;
}
bool locks_is_drag_key(uint16_t kc) {
    return kc == U_DLCK || kc == U_DMID || kc == U_DRCK;
}
bool locks_is_mod_key(uint16_t kc) {
    return kc >= U_LCTL_LOCK && kc <= U_LGUI_LOCK;
}
bool locks_process_key(uint16_t kc, bool pressed) {
    (void)pressed;
    return locks_is_drag_key(kc) || locks_is_mod_key(kc);
}
void locks_note_key(uint16_t kc, bool pressed) {
    (void)kc;
    (void)pressed;
}
bool wheel_is_cell_event(const keyrecord_t *r) {
    return IS_KEYEVENT(r->event) && r->event.key.row == 28 && r->event.key.col >= 1 && r->event.key.col <= 4;
}
bool wheel_process_record(uint16_t kc, keyrecord_t *r) {
    (void)r;
    return !IS_MOUSEKEY_WHEEL(kc);
}
void clear_oneshot_layer_state(oneshot_fullfillment_t s) {
    (void)s;
    osl_cleared++;
}
void clear_oneshot_mods(void) {
    osm_cleared++;
}
void wait_ms(uint32_t ms) {
    (void)ms;
}

static vial_tap_dance_entry_t tap_dances[4] = {
    {U_OSCR, KC_NO, KC_NO, KC_NO, 200},       /* TD(0): TAP is a oneshot key */
    {KC_A, KC_NO, KC_NO, KC_NO, 200},         /* TD(1): TAP is a letter */
    {KC_MS_BTN1, KC_NO, KC_NO, KC_NO, 200},   /* TD(2): TAP is a click */
    {KC_NO, MO(1), KC_NO, KC_NO, 200},        /* TD(3): only a layer HOLD */
};
int dynamic_keymap_get_tap_dance(uint8_t index, vial_tap_dance_entry_t *entry) {
    if (index >= 4) {
        return -1;
    }
    *entry = tap_dances[index];
    return 0;
}

/* ---- helpers ---- */

enum { PHYS, MAGIC, GESTURE_CELL, WHEEL_CELL, COMBO };

static bool event(uint16_t kc, bool pressed, int source, uint8_t tap_count) {
    keyrecord_t r = {0};
    r.event.type  = source == COMBO ? COMBO_EVENT : KEY_EVENT;
    r.event.pressed = pressed;
    switch (source) {
        case MAGIC:
            r.event.key = (keypos_t){.row = VIAL_MATRIX_MAGIC, .col = VIAL_MATRIX_MAGIC};
            break;
        case GESTURE_CELL:
            r.event.key = (keypos_t){.row = 31, .col = 0};
            break;
        case WHEEL_CELL:
            r.event.key = (keypos_t){.row = 28, .col = 1};
            break;
        default:
            r.event.key = (keypos_t){.row = 1, .col = 4};
            break;
    }
    r.tap.count = tap_count;
    bool to_qmk = process_record_mouse(kc, &r);
    if (to_qmk) {
        post_process_record_mouse(kc, &r);
    }
    return to_qmk;
}

static void tap(uint16_t kc, int source) {
    event(kc, true, source, 0);
    event(kc, false, source, 0);
}

static void fresh(void) {
    modes_reset();
    modes_new_pass();
    osl_cleared = osm_cleared = steps = 0;
    injecting   = false;
}

/* ---- oneshots ---- */

static void test_oneshot_ends(void) {
    fresh();
    tap(U_OSCR, PHYS);
    CHECK(modes_active(KQ_MODE_SCROLL), "O.SCR did not arm");
    tap(KC_A, PHYS);
    CHECK(!modes_active(KQ_MODE_SCROLL), "letter did not end O.SCR");

    fresh();
    tap(U_OSCR, PHYS);
    tap(KC_MS_BTN1, PHYS);
    CHECK(!modes_active(KQ_MODE_SCROLL), "click did not end O.SCR");

    fresh();
    tap(U_OSCR, PHYS);
    tap(U_OSCR, PHYS);
    CHECK(!modes_active(KQ_MODE_SCROLL), "second O.SCR did not disarm");

    fresh();
    tap(U_OGES, PHYS);
    tap(U_OSCR, PHYS);
    CHECK(modes_active(KQ_MODE_SCROLL) && !modes_active(KQ_MODE_GESTURE), "oneshots not exclusive");
}

static void test_oneshot_survives(void) {
    static const uint16_t keep[] = {KC_MS_WH_UP, MO(1), U_SCR_FASTER, U_LCTL_LOCK, U_SHOW, U_SSCR};
    for (unsigned i = 0; i < sizeof keep / sizeof keep[0]; i++) {
        fresh();
        tap(U_OSCR, PHYS);
        tap(keep[i], PHYS);
        CHECK(modes_active(KQ_MODE_SCROLL), "key 0x%04X ended O.SCR", keep[i]);
    }
    /* Our own taps: gesture and wheel cells, whatever they hold. */
    fresh();
    tap(U_OSCR, PHYS);
    tap(KC_A, GESTURE_CELL);
    tap(KC_A, WHEEL_CELL);
    injecting = true;
    tap(KC_B, PHYS);
    injecting = false;
    CHECK(modes_active(KQ_MODE_SCROLL), "an injected tap ended O.SCR");
}

static void test_tap_hold_and_dances(void) {
    /* LT / MT: a hold is not the next key, a tap is (on release). */
    fresh();
    tap(U_OSCR, PHYS);
    event(LT(1, KC_A), true, PHYS, 0);
    event(LT(1, KC_A), false, PHYS, 0); /* released as a hold */
    CHECK(modes_active(KQ_MODE_SCROLL), "LT hold ended O.SCR");
    event(LT(1, KC_A), true, PHYS, 1);
    CHECK(modes_active(KQ_MODE_SCROLL), "LT press ended O.SCR before tap was decided");
    event(LT(1, KC_A), false, PHYS, 1);
    CHECK(!modes_active(KQ_MODE_SCROLL), "LT tap did not end O.SCR");

    /* Tap dances count on press, unless their TAP is a mouse control. */
    fresh();
    tap(U_OSCR, PHYS);
    event(TD(0), true, PHYS, 0);
    CHECK(modes_active(KQ_MODE_SCROLL), "TD with an O.SCR TAP ended O.SCR");
    event(TD(0), false, PHYS, 0);
    event(TD(3), true, PHYS, 0);
    CHECK(modes_active(KQ_MODE_SCROLL), "TD with only a layer HOLD ended O.SCR");
    event(TD(3), false, PHYS, 0);
    event(TD(1), true, PHYS, 0);
    CHECK(!modes_active(KQ_MODE_SCROLL), "TD with a letter TAP did not end O.SCR");
}

/* ---- magic latches ---- */

static void test_magic_latches(void) {
    /* A tap dance TAP of D.SCR (press and release in one pass) lasts until
     * the next key, but clicks and drag locks do not end it. */
    fresh();
    tap(U_DSCR, MAGIC);
    CHECK(modes_active(KQ_MODE_SCROLL), "magic D.SCR TAP off");
    tap(KC_MS_BTN1, PHYS);
    tap(U_DLCK, PHYS);
    event(TD(2), true, PHYS, 0); /* a tap dance whose TAP clicks */
    event(TD(2), false, PHYS, 0);
    tap(U_GES, PHYS); /* another mode key */
    CHECK(modes_active(KQ_MODE_SCROLL), "click / drag lock / mode key ended the latch");
    tap(KC_A, PHYS);
    CHECK(!modes_active(KQ_MODE_SCROLL), "letter did not end the latch");

    /* A HOLD of it (released in a later pass) latches for good. */
    fresh();
    event(U_DSCR, true, MAGIC, 0);
    modes_new_pass();
    event(U_DSCR, false, MAGIC, 0);
    tap(KC_A, PHYS);
    CHECK(modes_active(KQ_MODE_SCROLL), "magic D.SCR HOLD ended by a letter");

    /* A magic TAP of SCR arms the oneshot; a gesture cell counts as magic. */
    fresh();
    tap(U_SCR, GESTURE_CELL);
    CHECK(modes_active(KQ_MODE_SCROLL), "SCR on a gesture cell did not arm the oneshot");

    /* S.SCR TAP latch: a letter ends it, a click does not. */
    fresh();
    tap(U_SSCR, MAGIC);
    CHECK(modes_slow_scroll_active(), "S.SCR TAP latch off");
    tap(KC_MS_BTN1, PHYS);
    CHECK(modes_slow_scroll_active(), "click ended the S.SCR latch");
    tap(KC_A, PHYS);
    CHECK(!modes_slow_scroll_active(), "letter did not end the S.SCR latch");
}

/* ---- QMK's oneshots ---- */

static void test_qmk_oneshots(void) {
    fresh();
    CHECK(!event(U_SCR_FASTER, true, PHYS, 0), "settings key reached QMK");
    CHECK(osl_cleared == 1 && steps == 1, "settings key: OSL %d steps %d", osl_cleared, steps);
    event(U_SCR_FASTER, false, PHYS, 0);
    CHECK_EQ(osl_cleared, 1);

    fresh();
    CHECK(event(KC_A, true, PHYS, 0), "letter did not reach QMK");
    CHECK(osl_cleared == 1 && osm_cleared == 0, "letter: OSL %d OSM %d", osl_cleared, osm_cleared);

    fresh();
    event(KC_MS_BTN1, true, PHYS, 0);
    CHECK(osl_cleared == 1 && osm_cleared == 1, "click: OSL %d OSM %d", osl_cleared, osm_cleared);

    fresh();
    event(MO(1), true, PHYS, 0);
    CHECK_EQ(osl_cleared, 0);

    fresh();
    CHECK(!event(KC_MS_WH_UP, true, PHYS, 0), "WH key reached QMK");
    CHECK_EQ(osl_cleared, 1);
}

int main(void) {
    test_oneshot_ends();
    test_oneshot_survives();
    test_tap_hold_and_dances();
    test_magic_latches();
    test_qmk_oneshots();
    return check_done("mouse keys");
}
