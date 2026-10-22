// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Core0 side of the USB host: turns queued events (hid_queue.h) into the key
 * matrix and the mouse / media input.
 *
 * Every input report of every device is a "source" with its own state: the
 * keys a keyboard report holds, the buttons a mouse report holds, the media
 * keys a consumer report holds. The matrix is all of them OR-ed together, so
 * two keyboards, or a keyboard whose keys come in two reports, never erase
 * each other's keys.
 *
 * The matrix is the identity layout Vial edits: HID usage u is at row
 * u / 8 + 1, column u % 8; modifiers E0..E7 are row 0; mouse buttons sit at
 * the usages of KC_MS_BTN1..8. Row 31 is never set from a device (it holds
 * the gesture cells, see keymaps/vial). */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hid_report.h"

#define KQ_MATRIX_ROWS 32
#define KQ_ROW_MODIFIERS 0

/* Processes queued events into matrix (KQ_MATRIX_ROWS rows of 8 columns),
 * stopping at the first report that changes it (QMK compares the matrix once
 * per scan, so a press and release in one scan would vanish) or after a
 * bounded number of events. True if the matrix changed. */
bool hid_input_scan(uint8_t *matrix);

/* Hooks into the keymap (weak defaults do nothing). */
void kq_mouse_report(const hid_mouse_t *mouse);    /* motion, wheel, pan; buttons are in the matrix */
void kq_mouse_scan_end(void);                       /* once per scan, after the reports */
void kq_mouse_device_lost(void);                    /* a mouse went away, or the last device */
void kq_vendor_report(uint16_t page, const uint8_t *data, uint8_t len);

typedef struct {
    uint16_t keyboard;
    uint16_t mouse;
    uint16_t consumer;
    uint16_t system;
    uint16_t other;
    uint16_t last_consumer; /* usage sent to the host */
    uint16_t last_system;
    uint16_t last_other_page;
    uint16_t last_buttons;
    int16_t  last_wheel; /* last non-zero, signed as the device sent it */
    int16_t  last_pan;
    uint16_t last_undefined;
    uint16_t unknown_reports; /* no device or no matching report */
} hid_input_stats_t;

hid_input_stats_t hid_input_stats(void);
