#include "keycodes.h"
#include "keycode_config.h"
#include "keymap_japanese.h"
#include "quantum_keycodes.h"

#include "os_key_override.h"

//
// Perform as a US keyboard on JP systems
//
static const key_override_t *us_key_on_jp_os_overrides[] = {
    // KC_GRV override depends on OS
    w_shift(KC_GRV, JP_TILD),
    w_shift(KC_2, JP_AT),
    w_shift(KC_6, JP_CIRC),
    w_shift(KC_7, JP_AMPR),
    w_shift(KC_8, JP_ASTR),
    w_shift(KC_9, JP_LPRN),
    w_shift(KC_0, JP_RPRN),
    w_shift(KC_MINS, JP_UNDS),
    wo_shift(KC_EQL, JP_EQL),
    w_shift(KC_EQL, JP_PLUS),
    wo_shift(KC_LBRC, JP_LBRC),
    w_shift(KC_LBRC, JP_LCBR),
    wo_shift(KC_RBRC, JP_RBRC),
    w_shift(KC_RBRC, JP_RCBR),
    wo_shift(KC_BSLS, JP_BSLS),
    w_shift(KC_BSLS, JP_PIPE),
    w_shift(KC_SCLN, JP_COLN),
    wo_shift(KC_QUOT, JP_QUOT),
    w_shift(KC_QUOT, JP_DQUO),
};

_Static_assert(OS_OVERRIDE_LEN(us_key_on_jp_os_overrides) + 1 <= OS_KEY_OVERRIDE_ENTRIES,
               "US-on-JP OS overrides exceed OS_KEY_OVERRIDE_ENTRIES");

// If ALT+GRV is pressed, send JP_ZKHK on windows
// To do this, mask ALT in override config
static const key_override_t *grv_override_jp_win = {&ko_make_with_layers_and_negmods(0, KC_GRV, JP_GRV, ~0, (uint8_t)MOD_MASK_SHIFT | MOD_MASK_ALT)};

// Always override KC_GRV to JP_GRV on Mac
static const key_override_t *grv_override_jp_mac = {wo_shift(KC_GRV, JP_GRV)};

void register_us_key_on_jp_os_overrides(void) {
    register_os_key_override_list(us_key_on_jp_os_overrides, OS_OVERRIDE_LEN(us_key_on_jp_os_overrides));
    register_os_key_override(keymap_config.swap_lalt_lgui ? grv_override_jp_mac : grv_override_jp_win);
}