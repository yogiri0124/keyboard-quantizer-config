// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Custom keycodes, in the order of vial.json customKeycodes (QK_KB_0..).
 * Keymaps saved in the device refer to these numbers: only ever append. */

#pragma once

#include "keycodes.h"

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

_Static_assert((int)U_CLR == (int)QK_KB_25, "vial.json customKeycodes order must match QK_KB_0..25");
