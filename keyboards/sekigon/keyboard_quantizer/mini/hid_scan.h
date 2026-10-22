// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* A quick look at a report descriptor, for core1 at mount time (core0 does
 * the full compile, parser/hid_desc.h): whether the device uses Report IDs,
 * and where its lock LEDs are. Pure C, stateless. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool    uses_report_ids;
    bool    has_led_output;  /* an Output item in the LED page */
    uint8_t led_report_id;   /* that output report's ID, 0 without IDs */
} hid_scan_t;

void hid_scan(const uint8_t *desc, uint16_t len, hid_scan_t *out);
