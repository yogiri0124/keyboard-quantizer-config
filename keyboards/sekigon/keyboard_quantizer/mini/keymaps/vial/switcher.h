// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Window switching without holding Alt (A.TAB / A.STAB, like ZMK's
 * swapper). The first press holds Alt down and taps Tab (Shift+Tab), which
 * opens the window switcher; later presses only tap, moving through it.
 * Alt goes up, choosing the window, when a layer that was on at the first
 * press goes off (the hold key that brought the layer is let go), on any
 * other key press, or, when no layer was on, SWITCHER_TIMEOUT_MS after the
 * last press (a layer kept on by L.LCK counts as none: it does not go off).
 * With Alt already down (A.LCK, the Alt key) the keys only tap and leave
 * Alt alone. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef SWITCHER_TIMEOUT_MS
#    define SWITCHER_TIMEOUT_MS 1000
#endif

/* Every key event: handles A.TAB / A.STAB (true); any other key press ends
 * a switch first. */
bool switcher_process_key(uint16_t keycode, bool pressed);
bool switcher_active(void);
void switcher_task(void);  /* layer off, timeout */
void switcher_clear(void); /* RST / CLR / unplug */
