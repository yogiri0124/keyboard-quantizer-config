// Copyright 2023 sekigon-gonnoc
// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Raw HID: answers VIA and Vial alike (via.c is built with its handler
 * renamed, see rules.mk), and takes the custom values of the Mouse menu
 * (settings.c) whichever of the two asks. */

#include <stdbool.h>

#include "raw_hid.h"
#include "via.h"
#include "vial.h"
#include "settings.h"
#include "default_config.h"

extern void raw_hid_receive_vial(uint8_t *data, uint8_t length);
extern void raw_hid_receive_qmk(uint8_t *data, uint8_t length);

/* The VIA protocol version each client expects. */
#define VIAL_SUPPORT_VIA_VERSION 0x0009
#define QMK_SUPPORT_VIA_VERSION 0x000C

/* Vial asked for the keyboard id: from then on it is Vial talking. */
static bool vial_client;

static bool is_custom_value_command(uint8_t command) {
    return command == id_custom_set_value || command == id_custom_get_value || command == id_custom_save;
}

void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (data == NULL || length == 0) {
        return;
    }
    uint8_t command = data[0];
    if (command == id_get_protocol_version && length >= 3) {
        uint16_t version = vial_client ? VIAL_SUPPORT_VIA_VERSION : QMK_SUPPORT_VIA_VERSION;
        data[1]          = (uint8_t)(version >> 8);
        data[2]          = (uint8_t)version;
        raw_hid_send(data, length);
        return;
    }
    if (command == id_vial_prefix && length >= 2 && data[1] == vial_get_keyboard_id) {
        vial_client = true; /* vial.c answers it */
    }
    /* Vial's via.c reads 0x07..0x09 as lighting and never passes channel 0
     * on: take the Mouse menu here, for both clients. */
    if (is_custom_value_command(command) && length >= 2 && data[1] == id_custom_channel) {
        settings_via_command(data, length);
        raw_hid_send(data, length);
        return;
    }
    if (vial_client) {
        raw_hid_receive_vial(data, length);
    } else {
        raw_hid_receive_qmk(data, length);
    }
    /* A keymap reset (Vial / VIA) empties tap dances, combos, key overrides
     * and QMK Settings too: put default.vil's back. */
    if (command == id_dynamic_keymap_reset) {
        default_config_write();
    }
}

/* Commands neither via.c knows. A custom value command wrapped in
 * id_unhandled (as some VIA custom UI sends it) is the Mouse menu too. */
void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (data == NULL || length == 0) {
        return;
    }
    if (data[0] == id_unhandled && length > 2 && is_custom_value_command(data[1])) {
        settings_via_command(&data[1], (uint8_t)(length - 1));
        return;
    }
    data[0] = id_unhandled;
}
