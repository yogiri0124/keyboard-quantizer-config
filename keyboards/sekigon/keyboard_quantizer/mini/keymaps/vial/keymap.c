/* Copyright 2020 sekigon-gonnoc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H

#include "virtser.h"
#include "vial.h"
#include "keymap_introspection.h"

#include "cli.h"
#include "default_config.h"
#include "identity.h"
#include "mouse.h"
#include "mouse_keys.h"
#include "os_key_override.h"
#include "settings.h"

/* The default keymap (keymaps[]): default.vil, the layout as Vial saves it,
 * built in (see default_config.c). */
#define KQ_DEFAULT_KEYMAP
#include "default_vil.h"

/* QMK's keycode lookup (quantum/keymap_common.c), with one addition: layer
 * 0's gesture cells are layer 1's. Gestures tap their cell like a key
 * (mouse.c), so QMK resolves them as it does any key, also when it holds a
 * tap back while a tap-hold key decides. Layer 0 is the base layout, whose
 * gesture cells stay empty, so it reads layer 1's. */
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    /* No keycode processing while Vial unlocks. */
    if (vial_unlock_in_progress) {
        return KC_NO;
    }
    if (key.row == VIAL_MATRIX_MAGIC && key.col == VIAL_MATRIX_MAGIC) {
        return g_vial_magic_keycode_override;
    }
    if (key.row >= MATRIX_ROWS || key.col >= MATRIX_COLS) {
        return KC_NO;
    }
    if (key.row == GESTURE_ROW && layer == 0 && DYNAMIC_KEYMAP_LAYER_COUNT > 1) {
        layer = 1;
    }
    return keycode_at_keymap_location(layer, key.row, key.col);
}

#if defined(ENCODER_MAP_ENABLE) || defined(DIP_SWITCH_MAP_ENABLE)
#    error "keymap_key_to_keycode above does not handle encoder or DIP switch maps"
#endif

static int8_t virtser_send_char(uint8_t c) {
    virtser_send(c);
    return 0;
}

void keyboard_pre_init_user(void) {
    cli_init();
    print_set_sendchar(virtser_send_char);
}

/* QMK clears the EEPROM (first start, EE_CLR): it has just reset the VIA
 * part to the bare keymap, so default.vil's other settings go in now. */
void eeconfig_init_user(void) {
    mouse_reset_latches();
    settings_init();
    default_config_write();
}

void keyboard_post_init_user(void) {
    /* Vial has loaded its key overrides; the JP/US ones go after them. */
    os_key_override_init();
    mouse_init();
    settings_load();
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    return process_record_mouse(keycode, record);
}

void post_process_record_user(uint16_t keycode, keyrecord_t *record) {
    post_process_record_mouse(keycode, record);
}

void housekeeping_task_user(void) {
    mouse_housekeeping();
    os_override_refresh();
    cli_exec();
}
