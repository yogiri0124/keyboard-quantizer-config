// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Latching keys: drag locks (D.LCK / D.MID / D.RCK hold a mouse button down)
 * and modifier locks (C.LCK / S.LCK / A.LCK / W.LCK hold a modifier down).
 * Each key flips its lock; clicking that button, or pressing that modifier
 * key, also releases it. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DRAG_LOCK_COUNT 3 /* L, M, R */
#define MOD_LOCK_COUNT 4  /* Ctrl, Shift, Alt, GUI */

/* Handles a lock key; false if keycode is not one. */
bool locks_process_key(uint16_t keycode, bool pressed);
bool locks_is_drag_key(uint16_t keycode);
bool locks_is_mod_key(uint16_t keycode);
/* Every key event: a physical modifier releases its lock. */
void locks_note_key(uint16_t keycode, bool pressed);
/* A mouse button keycode was pressed: its drag lock ends. */
void locks_button_pressed(uint16_t keycode);

uint8_t locks_buttons(void); /* HID button bits the drag locks hold */
bool    locks_drag(uint8_t i);
bool    locks_mod(uint8_t i);

void locks_task(void);  /* keeps locked modifiers registered */
void locks_clear(void); /* RST / CLR / unplug */
