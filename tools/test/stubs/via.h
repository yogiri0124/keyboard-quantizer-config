/* Host-test stand-in for quantum/via.h: the command ids the keymap uses. */
#pragma once

enum {
    id_dynamic_keymap_reset = 0x06,
    id_custom_set_value     = 0x07,
    id_custom_get_value     = 0x08,
    id_custom_save          = 0x09,
    id_unhandled            = 0xFF,
};
enum { id_custom_channel = 0 };
