#pragma once

#include <stdint.h>
#include "keycodes.h"

enum {
    KEY_OS_OVERRIDE_DISABLE,
    US_KEY_JP_OS_OVERRIDE_DISABLE,
    JP_KEY_US_OS_OVERRIDE_DISABLE,
};

enum {
    /* 0-5  Scroll: hold / toggle / oneshot / slow / faster / slower */
    U_SCR = QK_KB_0,
    U_DSCR,
    U_OSCR,
    U_SSCR,
    U_SCR_FASTER,
    U_SCR_SLOWER,
    /* 6-10 Gesture: hold / toggle / oneshot / easier / harder */
    U_GES,
    U_TGES,
    U_OGES,
    U_THR_EASIER,
    U_THR_HARDER,
    /* 11-15 Cursor: hold / toggle / oneshot / faster / slower */
    U_SLOW,
    U_TSLO,
    U_OSLO,
    U_DPI_FASTER,
    U_DPI_SLOWER,
    /* 16-18 Drag lock: left / middle / right */
    U_DLCK,
    U_DMID,
    U_DRCK,
    /* 19-20 Settings: print / reset saved values + all latches */
    U_SHOW,
    U_RST,
    /* 21-24 Modifier lock: Ctrl / Shift / Alt / GUI */
    U_LCTL_LOCK,
    U_LSFT_LOCK,
    U_LALT_LOCK,
    U_LGUI_LOCK,
    /* 25 Clear all latches (does not write saved values) */
    U_CLR,
};

#define USER_SCROLL_DIV_MIN 8
#define USER_SCROLL_DIV_MAX 80
#define USER_SCROLL_DIV_DEFAULT 32
#define USER_GESTURE_THRESHOLD_MIN 20
#define USER_GESTURE_THRESHOLD_MAX 120
#define USER_GESTURE_THRESHOLD_DEFAULT 50
#define USER_CURSOR_SCALE_MIN 4
#define USER_CURSOR_SCALE_MAX 32
#define USER_CURSOR_SCALE_DEFAULT 16

typedef union {
    uint32_t raw;
    struct {
        uint8_t key_os_override : 2;
        uint8_t reserved : 6;
        uint8_t scroll_div;
        uint8_t gesture_threshold;
        /* USER_CURSOR_SCALE_MAX needs 6 bits. The top two briefly held wheel
         * flags in a test build; keeping them apart ignores whatever is left. */
        uint8_t cursor_scale : 6;
        uint8_t reserved2 : 2;
    };
} user_config_t;

_Static_assert(sizeof(user_config_t) == 4, "user_config_t must be 4 bytes");
_Static_assert(USER_CURSOR_SCALE_MAX < 64, "cursor scale must fit 6 bits");
_Static_assert((int)U_CLR == (int)QK_KB_25, "vial customKeycodes order must match QK_KB_0..25");
_Static_assert(USER_SCROLL_DIV_DEFAULT >= USER_SCROLL_DIV_MIN && USER_SCROLL_DIV_DEFAULT <= USER_SCROLL_DIV_MAX,
               "default scroll div must be in range");
_Static_assert(USER_GESTURE_THRESHOLD_DEFAULT >= USER_GESTURE_THRESHOLD_MIN &&
                   USER_GESTURE_THRESHOLD_DEFAULT <= USER_GESTURE_THRESHOLD_MAX,
               "default gesture threshold must be in range");
_Static_assert(USER_CURSOR_SCALE_DEFAULT >= USER_CURSOR_SCALE_MIN && USER_CURSOR_SCALE_DEFAULT <= USER_CURSOR_SCALE_MAX,
               "default cursor scale must be in range");

extern user_config_t user_config;

void user_os_override_set(uint8_t mode);
uint8_t user_os_override_get(void);
const char *user_os_override_name(uint8_t mode);
