// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Decodes a received input report against the compiled descriptor
 * (hid_desc.h) into what it means: held keys, mouse motion and buttons, or
 * pressed consumer / system usages. Pure C, testable on the host. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hid_desc.h"

#define HID_USAGE_LIST_MAX 8

typedef struct {
    uint8_t bits[256 / 8]; /* keyboard usages 0x00..0xFF held */
} hid_keys_t;

typedef struct {
    bool     has_buttons; /* the report carries buttons (a wheel-only report does not) */
    uint16_t buttons;     /* bit n = button n+1 */
    int16_t  x;
    int16_t  y;
    int16_t  wheel;     /* positive = up */
    int16_t  pan;       /* AC Pan as the device sends it */
    uint16_t undefined; /* bits of fields nothing here understands (CLI diagnostics) */
} hid_mouse_t;

typedef struct {
    uint8_t  count;
    uint16_t usages[HID_USAGE_LIST_MAX];
} hid_usage_list_t;

typedef struct {
    hid_kind_t kind;
    uint8_t    report_index; /* within the device; with the interface it names the source */
    uint8_t    report_id;
    uint16_t   collection_page;
    union {
        hid_keys_t       keys;   /* HID_KIND_KEYBOARD */
        hid_mouse_t      mouse;  /* HID_KIND_MOUSE */
        hid_usage_list_t usages; /* HID_KIND_CONSUMER, HID_KIND_SYSTEM */
    };
    const uint8_t *payload; /* report data after the Report ID (HID_KIND_OTHER) */
    uint8_t        payload_len;
} hid_decoded_t;

/* False if the interface has no device, the report is unknown, or it carries
 * nothing to act on (a keyboard reporting a rollover error keeps its state). */
bool hid_report_decode(uint8_t interface, const uint8_t *data, uint8_t len, hid_decoded_t *out);
