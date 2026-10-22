// Copyright 2023 sekigon-gonnoc
// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Dynamic keymap macros, replacing quantum/dynamic_keymap.c's player (renamed
 * in rules.mk) so that a delay saved by VIA plays too: Vial stores a delay as
 * two bytes, VIA as 1-5 ASCII digits and a '|'. Otherwise it plays what
 * QMK's does: text, taps / presses / releases, Vial's 16-bit keycodes.
 *
 * The macro buffer is read through QMK's own accessors, so its place in the
 * EEPROM stays QMK's business. */

#include QMK_KEYBOARD_H

#include "vial.h"
#include "dynamic_keymap.h"
#include "send_string.h"

#ifndef DYNAMIC_KEYMAP_MACRO_DELAY
#    define DYNAMIC_KEYMAP_MACRO_DELAY TAP_CODE_DELAY
#endif

#define VIA_DELAY_DIGITS 5

typedef struct {
    uint16_t pos;
    uint16_t size;
} reader_t;

/* The next byte, 0 past the end (which also ends a macro). */
static uint8_t next(reader_t *r) {
    uint8_t b = 0;
    if (r->pos < r->size) {
        dynamic_keymap_macro_get_buffer(r->pos, 1, &b);
    }
    r->pos++;
    return b;
}

static uint8_t peek(const reader_t *r, uint16_t ahead) {
    uint8_t b = 0;
    if (r->pos + ahead < r->size) {
        dynamic_keymap_macro_get_buffer((uint16_t)(r->pos + ahead), 1, &b);
    }
    return b;
}

/* Vial: 16-bit keycodes travel without zero bytes, 0xXX00 as 0xFFXX. */
static uint16_t decode_keycode(uint8_t lo, uint8_t hi) {
    uint16_t kc = (uint16_t)(lo | (hi << 8));
    return kc > 0xFF00 ? (uint16_t)((kc & 0xFF) << 8) : kc;
}

/* A delay, in either encoding. False if the macro ends inside it. */
static bool read_delay(reader_t *r, uint32_t *ms) {
    uint32_t value = 0;
    for (uint16_t i = 0; i <= VIA_DELAY_DIGITS; i++) {
        uint8_t c = peek(r, i);
        if (i < VIA_DELAY_DIGITS && c >= '0' && c <= '9') {
            value = value * 10 + (uint32_t)(c - '0');
            continue;
        }
        /* Only a '|' after at least one digit makes it VIA's. */
        if (c == '|' && i != 0) {
            r->pos = (uint16_t)(r->pos + i + 1);
            *ms    = value;
            return true;
        }
        break;
    }
    uint8_t d0 = next(r), d1 = next(r);
    if (d0 == 0 || d1 == 0) {
        return false;
    }
    /* Neither byte may be 0: each counts from 1, base 255. */
    *ms = (uint32_t)(d0 - 1) + (uint32_t)(d1 - 1) * 255;
    return true;
}

void dynamic_keymap_macro_send(uint8_t id) {
    reader_t r = {0, dynamic_keymap_macro_get_buffer_size()};
    if (id >= DYNAMIC_KEYMAP_MACRO_COUNT || r.size == 0) {
        return;
    }
    /* A buffer that does not end in 0 is half written: play nothing. */
    r.pos = (uint16_t)(r.size - 1);
    if (next(&r) != 0) {
        return;
    }
    /* Macro id starts after id zero bytes. */
    r.pos = 0;
    while (id > 0) {
        if (r.pos >= r.size) {
            return;
        }
        if (next(&r) == 0) {
            id--;
        }
    }

    for (;;) {
        char data[4] = {0, 0, 0, 0};
        data[0]      = (char)next(&r);
        if (data[0] == 0) {
            return;
        }
        if (data[0] != SS_QMK_PREFIX) {
            send_string_with_delay(data, DYNAMIC_KEYMAP_MACRO_DELAY);
            continue;
        }
        uint8_t code = next(&r);
        switch (code) {
            case SS_TAP_CODE:
            case SS_DOWN_CODE:
            case SS_UP_CODE:
                data[1] = (char)code;
                data[2] = (char)next(&r);
                if (data[2] == 0) {
                    return;
                }
                send_string(data);
                break;
            case VIAL_MACRO_EXT_TAP:
            case VIAL_MACRO_EXT_DOWN:
            case VIAL_MACRO_EXT_UP: {
                uint8_t lo = next(&r), hi = 0;
                if (lo == 0 || (hi = next(&r)) == 0) {
                    return;
                }
                uint16_t kc = decode_keycode(lo, hi);
                if (code == VIAL_MACRO_EXT_TAP) {
                    vial_keycode_tap(kc);
                } else if (code == VIAL_MACRO_EXT_DOWN) {
                    vial_keycode_down(kc);
                } else {
                    vial_keycode_up(kc);
                }
                break;
            }
            case SS_DELAY_CODE: {
                uint32_t ms;
                if (!read_delay(&r, &ms)) {
                    return;
                }
                while (ms--) {
                    wait_ms(1);
                }
                break;
            }
            default: /* 0 ends the macro; anything else is unknown */
                return;
        }
    }
}
