// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

/* Dynamic keymap macros, replacing quantum/dynamic_keymap.c's (renamed in
 * rules.mk): also plays Vial's extended keycodes and accepts both the VIA and
 * the Vial encoding of delays. */

#include QMK_KEYBOARD_H

#include <string.h>

#include "vial.h"
#include "dynamic_keymap.h"
#include "send_string.h"

#ifdef VIA_ENABLE
#    include "via.h"
#    define DYNAMIC_KEYMAP_EEPROM_START (VIA_EEPROM_CONFIG_END)
#else
#    include "eeconfig.h"
#    define DYNAMIC_KEYMAP_EEPROM_START (EECONFIG_SIZE)
#endif

#ifdef ENCODER_ENABLE
#    include "encoder.h"
#else
#    define NUM_ENCODERS 0
#endif

#ifndef DYNAMIC_KEYMAP_EEPROM_MAX_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR (TOTAL_EEPROM_BYTE_COUNT - 1)
#endif

// If DYNAMIC_KEYMAP_EEPROM_ADDR not explicitly defined in config.h,
#ifndef DYNAMIC_KEYMAP_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_EEPROM_ADDR DYNAMIC_KEYMAP_EEPROM_START
#endif

// Encoders are located right after the dynamic keymap
#define VIAL_ENCODERS_EEPROM_ADDR (DYNAMIC_KEYMAP_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2))
#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR VIAL_ENCODERS_EEPROM_ADDR

#define VIAL_ENCODERS_SIZE (NUM_ENCODERS * DYNAMIC_KEYMAP_LAYER_COUNT * 2 * 2)

// QMK settings area is just past encoders
#define VIAL_QMK_SETTINGS_EEPROM_ADDR (VIAL_ENCODERS_EEPROM_ADDR + VIAL_ENCODERS_SIZE)

#ifdef QMK_SETTINGS
#include "qmk_settings.h"
#define VIAL_QMK_SETTINGS_SIZE (sizeof(qmk_settings_t))
#else
#define VIAL_QMK_SETTINGS_SIZE 0
#endif

// Tap-dance
#define VIAL_TAP_DANCE_EEPROM_ADDR (VIAL_QMK_SETTINGS_EEPROM_ADDR + VIAL_QMK_SETTINGS_SIZE)

#ifdef VIAL_TAP_DANCE_ENABLE
#define VIAL_TAP_DANCE_SIZE (sizeof(vial_tap_dance_entry_t) * VIAL_TAP_DANCE_ENTRIES)
#else
#define VIAL_TAP_DANCE_SIZE 0
#endif

// Combos
#define VIAL_COMBO_EEPROM_ADDR (VIAL_TAP_DANCE_EEPROM_ADDR + VIAL_TAP_DANCE_SIZE)

#ifdef VIAL_COMBO_ENABLE
#define VIAL_COMBO_SIZE (sizeof(vial_combo_entry_t) * VIAL_COMBO_ENTRIES)
#else
#define VIAL_COMBO_SIZE 0
#endif

// Key overrides
#define VIAL_KEY_OVERRIDE_EEPROM_ADDR (VIAL_COMBO_EEPROM_ADDR + VIAL_COMBO_SIZE)

#ifdef VIAL_KEY_OVERRIDE_ENABLE
#define VIAL_KEY_OVERRIDE_SIZE (sizeof(vial_key_override_entry_t) * VIAL_KEY_OVERRIDE_ENTRIES)
#else
#define VIAL_KEY_OVERRIDE_SIZE 0
#endif

// Dynamic macro
#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR
#    define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR (VIAL_KEY_OVERRIDE_EEPROM_ADDR + VIAL_KEY_OVERRIDE_SIZE)
#endif

// Dynamic macros are stored after the keymaps and use what is available
// up to and including DYNAMIC_KEYMAP_EEPROM_MAX_ADDR.
#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE
#    define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR - DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + 1)
#endif

#ifndef DYNAMIC_KEYMAP_MACRO_DELAY
#    define DYNAMIC_KEYMAP_MACRO_DELAY TAP_CODE_DELAY
#endif

static uint16_t decode_keycode(uint16_t kc) {
    /* map 0xFF01 => 0x0100; 0xFF02 => 0x0200, etc */
    if (kc > 0xFF00)
        return (kc & 0xFF) << 8;
    return kc;
}

void dynamic_keymap_macro_send(uint8_t id) {
    if (id >= DYNAMIC_KEYMAP_MACRO_COUNT) {
        return;
    }

    // Check the last byte of the buffer.
    // If it's not zero, then we are in the middle
    // of buffer writing, possibly an aborted buffer
    // write. So do nothing.
    void *p = (void *)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE - 1);
    if (eeprom_read_byte(p) != 0) {
        return;
    }

    // Skip N null characters
    // p will then point to the Nth macro
    p         = (void *)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR);
    void *end = (void *)(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE);
    while (id > 0) {
        // If we are past the end of the buffer, then the buffer
        // contents are garbage, i.e. there were not DYNAMIC_KEYMAP_MACRO_COUNT
        // nulls in the buffer.
        if (p == end) {
            return;
        }
        if (eeprom_read_byte(p) == 0) {
            --id;
        }
        ++p;
    }

    // Send the macro string one or three chars at a time
    // by making temporary 1 or 3 char strings
    char data[4] = {0, 0, 0, 0};
    // We already checked there was a null at the end of
    // the buffer, so this cannot go past the end
    while (1) {
        // Clear every pass: an extended tap leaves data[2..3] set, and the
        // next 1- or 3-char send_string() would then run off the end.
        memset(data, 0, sizeof(data));
        data[0] = eeprom_read_byte(p++);
        // Stop at the null terminator of this macro string
        if (data[0] == 0) {
            break;
        }
        if (data[0] == SS_QMK_PREFIX) {
            // If the char is magic, process it as indicated by the next character
            // (tap, down, up, delay)
            data[1] = eeprom_read_byte(p++);
            if (data[1] == 0)
                break;
            if (data[1] == SS_TAP_CODE || data[1] == SS_DOWN_CODE || data[1] == SS_UP_CODE) {
                // For tap, down, up, just stuff it into the array and send_string it
                data[2] = eeprom_read_byte(p++);
                if (data[2] != 0)
                    send_string(data);
            } else if (data[1] == VIAL_MACRO_EXT_TAP || data[1] == VIAL_MACRO_EXT_DOWN || data[1] == VIAL_MACRO_EXT_UP) {
                data[2] = eeprom_read_byte(p++);
                if (data[2] != 0) {
                    data[3] = eeprom_read_byte(p++);
                    if (data[3] != 0) {
                        uint16_t kc;
                        memcpy(&kc, &data[2], sizeof(kc));
                        kc = decode_keycode(kc);
                        switch (data[1]) {
                        case VIAL_MACRO_EXT_TAP:
                            vial_keycode_tap(kc);
                            break;
                        case VIAL_MACRO_EXT_DOWN:
                            vial_keycode_down(kc);
                            break;
                        case VIAL_MACRO_EXT_UP:
                            vial_keycode_up(kc);
                            break;
                        }
                    }
                }
            } else if (data[1] == SS_DELAY_CODE) {
                // Check delay code type is VIA(0xC) or not: VIA stores 1-5
                // ASCII digits and a '|'. Only a found '|' makes it VIA, so
                // five digits without one fall back to the Vial encoding.
                bool is_via = false;
                int  ms     = 0;
                for (int idx = 0; idx <= 5; idx++) {
                    uint8_t c = eeprom_read_byte(p + idx);
                    if (idx < 5 && c >= '0' && c <= '9') {
                        ms = ms * 10 + c - '0';
                    } else {
                        if (c == '|' && idx != 0) {
                            is_via = true;
                            p += idx + 1;
                        }
                        break;
                    }
                }

                if (!is_via) {
                    // For delay, decode the delay and wait_ms for that amount
                    uint8_t d0 = eeprom_read_byte(p++);
                    uint8_t d1 = eeprom_read_byte(p++);
                    if (d0 == 0 || d1 == 0) break;
                    // we cannot use 0 for these, need to subtract 1 and use 255 instead of 256 for delay calculation
                    ms = (d0 - 1) + (d1 - 1) * 255;
                }

                while (ms--) wait_ms(1);
            }
        } else {
            // If the char wasn't magic, just send it
            send_string_with_delay(data, DYNAMIC_KEYMAP_MACRO_DELAY);
        }
    }
}
