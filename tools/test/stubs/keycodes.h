/* Host-test stand-in for quantum/keycodes.h: the ranges and keycodes the
 * mouse code uses, with QMK's values. */
#pragma once

enum {
    KC_NO                   = 0x0000,
    KC_TRNS                 = 0x0001,
    KC_A                    = 0x0004,
    KC_B                    = 0x0005,
    KC_LEFT_CTRL            = 0x00E0,
    KC_LCTL                 = 0x00E0,
    KC_LSFT                 = 0x00E1,
    KC_LALT                 = 0x00E2,
    KC_LGUI                 = 0x00E3,
    KC_RIGHT_GUI            = 0x00E7,
    KC_MS_UP                = 0x00CD,
    KC_MS_DOWN              = 0x00CE,
    KC_MS_LEFT              = 0x00CF,
    KC_MS_RIGHT             = 0x00D0,
    KC_MS_BTN1              = 0x00D1,
    KC_MS_BTN2              = 0x00D2,
    KC_MS_BTN3              = 0x00D3,
    KC_MS_BTN8              = 0x00D8,
    KC_MS_WH_UP             = 0x00D9,
    KC_MS_WH_DOWN           = 0x00DA,
    KC_MS_WH_LEFT           = 0x00DB,
    KC_MS_WH_RIGHT          = 0x00DC,
    QK_MODS                 = 0x0100,
    QK_MOD_TAP              = 0x2000,
    QK_MOD_TAP_MAX          = 0x3FFF,
    QK_LAYER_TAP            = 0x4000,
    QK_LAYER_TAP_MAX        = 0x4FFF,
    QK_LAYER_MOD            = 0x5000,
    QK_LAYER_MOD_MAX        = 0x51FF,
    QK_TO                   = 0x5200,
    QK_TO_MAX               = 0x521F,
    QK_MOMENTARY            = 0x5220,
    QK_MOMENTARY_MAX        = 0x523F,
    QK_DEF_LAYER            = 0x5240,
    QK_DEF_LAYER_MAX        = 0x525F,
    QK_TOGGLE_LAYER         = 0x5260,
    QK_TOGGLE_LAYER_MAX     = 0x527F,
    QK_ONE_SHOT_LAYER       = 0x5280,
    QK_ONE_SHOT_LAYER_MAX   = 0x529F,
    QK_LAYER_TAP_TOGGLE     = 0x52C0,
    QK_LAYER_TAP_TOGGLE_MAX = 0x52DF,
    QK_TAP_DANCE            = 0x5700,
    QK_TAP_DANCE_MAX        = 0x57FF,
    QK_KB                   = 0x7E00,
    QK_KB_MAX               = 0x7E3F,
    QK_KB_0                 = 0x7E00,
    QK_KB_25                = 0x7E19,
};

#define IS_MOUSEKEY_BUTTON(code) (KC_MS_BTN1 <= (code) && (code) <= KC_MS_BTN8)
#define IS_MOUSEKEY_WHEEL(code) (KC_MS_WH_UP <= (code) && (code) <= KC_MS_WH_RIGHT)
#define IS_MODIFIER_KEYCODE(code) ((code) >= KC_LEFT_CTRL && (code) <= KC_RIGHT_GUI)
#define IS_QK_KB(code) ((code) >= QK_KB && (code) <= QK_KB_MAX)
#define IS_QK_MOD_TAP(code) ((code) >= QK_MOD_TAP && (code) <= QK_MOD_TAP_MAX)
#define IS_QK_LAYER_TAP(code) ((code) >= QK_LAYER_TAP && (code) <= QK_LAYER_TAP_MAX)
#define IS_QK_LAYER_MOD(code) ((code) >= QK_LAYER_MOD && (code) <= QK_LAYER_MOD_MAX)
#define IS_QK_TO(code) ((code) >= QK_TO && (code) <= QK_TO_MAX)
#define IS_QK_MOMENTARY(code) ((code) >= QK_MOMENTARY && (code) <= QK_MOMENTARY_MAX)
#define IS_QK_DEF_LAYER(code) ((code) >= QK_DEF_LAYER && (code) <= QK_DEF_LAYER_MAX)
#define IS_QK_TOGGLE_LAYER(code) ((code) >= QK_TOGGLE_LAYER && (code) <= QK_TOGGLE_LAYER_MAX)
#define IS_QK_ONE_SHOT_LAYER(code) ((code) >= QK_ONE_SHOT_LAYER && (code) <= QK_ONE_SHOT_LAYER_MAX)
#define IS_QK_LAYER_TAP_TOGGLE(code) ((code) >= QK_LAYER_TAP_TOGGLE && (code) <= QK_LAYER_TAP_TOGGLE_MAX)
#define IS_QK_TAP_DANCE(code) ((code) >= QK_TAP_DANCE && (code) <= QK_TAP_DANCE_MAX)
#define QK_LAYER_TAP_GET_TAP_KEYCODE(kc) ((kc) & 0xFF)
#define QK_MOD_TAP_GET_TAP_KEYCODE(kc) ((kc) & 0xFF)

#define MO(layer) (QK_MOMENTARY | (layer))
#define TD(n) (QK_TAP_DANCE | (n))
#define LT(layer, kc) (QK_LAYER_TAP | ((layer) << 8) | (kc))
#define MT(mod, kc) (QK_MOD_TAP | ((mod) << 8) | (kc))
