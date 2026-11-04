// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Layer Lock (L.LCK), like QMK's later QK_LAYER_LOCK: pressed while a
 * layer is on, it keeps the topmost one on after its hold key is let go;
 * pressed again on that layer, it lets it go. The lock holds the layer in
 * layer_state_set_user, so it works whatever turned the layer on: MO, LT,
 * a tap dance's HOLD. TO() and TG() of a locked layer unlock it first, so
 * they still do what they say; CLR / RST / unplugging unlock everything. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Include after QMK_KEYBOARD_H (layer_state_t). */

/* Every key event: handles L.LCK (true); TO / TG unlock what they move
 * off. */
bool          layer_lock_process_key(uint16_t keycode, bool pressed);
layer_state_t layer_lock_locked(void);
void          layer_lock_clear(void); /* CLR / RST / unplug: locked layers go off */
