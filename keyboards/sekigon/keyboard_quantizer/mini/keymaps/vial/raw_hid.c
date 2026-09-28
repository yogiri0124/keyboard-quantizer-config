#include <stdbool.h>

#include "raw_hid.h"
#include "via.h"
#include "vial.h"
#include "quantizer_mouse.h"

extern void raw_hid_receive_vial(uint8_t *data, uint8_t length);
extern void raw_hid_receive_qmk(uint8_t *data, uint8_t length);

#define VIAL_SUPPORT_VIA_VERSION 0x0009
#define QMK_SUPPORT_VIA_VERSION 0x000C

static bool is_vial_enabled = false;

static bool pre_raw_hid_receive(uint8_t *msg, uint8_t len) {
    bool _continue = true;

    if (msg == NULL || len == 0) {
        return false;
    }

    // Override VIA protocol version
    if (len >= 3 && msg[0] == id_get_protocol_version) {
        const uint16_t via_version = is_vial_enabled ? VIAL_SUPPORT_VIA_VERSION : QMK_SUPPORT_VIA_VERSION;
        msg[1]                     = via_version >> 8;
        msg[2]                     = via_version & 0xFF;

        _continue = false;
    } else if (len >= 2 && msg[0] == 0xfe) {
        switch (msg[1]) {
            case vial_get_keyboard_id: {
                is_vial_enabled = true;
                _continue = true; // vial id is set in vial.c
            } break;
        }
    }

    if (!_continue) {
        raw_hid_send(msg, len);
    }

    return _continue;
}

static bool is_custom_value_command(uint8_t command_id) {
    return command_id == id_custom_set_value || command_id == id_custom_get_value || command_id == id_custom_save;
}

void raw_hid_receive(uint8_t *data, uint8_t length) {
    if (!pre_raw_hid_receive(data, length)) {
        return;
    }
    // Vial via.c aliases 0x07/0x08/0x09 as lighting and never forwards channel 0.
    if (length >= 2 && is_custom_value_command(data[0]) && data[1] == id_custom_channel) {
        via_custom_value_command_kb(data, length);
        raw_hid_send(data, length);
        return;
    }
    if (is_vial_enabled) {
        raw_hid_receive_vial(data, length);
    } else {
        raw_hid_receive_qmk(data, length);
    }
}

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (data == NULL || length == 0) {
        return;
    }

    uint8_t *command_id = &(data[0]);

    // VIA custom UI for Vial wraps set/get/save in id_unhandled.
    if (*command_id == id_unhandled && length > 2 && is_custom_value_command(data[1])) {
        via_custom_value_command_kb(&data[1], (uint8_t)(length - 1));
        return;
    }
    if (is_custom_value_command(*command_id)) {
        via_custom_value_command_kb(data, length);
        return;
    }
    *command_id = id_unhandled;
}