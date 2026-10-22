// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* HID report descriptor compiler.
 *
 * When a device is mounted, its report descriptor is compiled once into a
 * table: for every input report (by Report ID) the list of fields, each with
 * its bit position, size, count, flags and usages. Decoding a report later is
 * then a lookup plus bit extraction, with no descriptor walking.
 *
 * Only Input items become fields. Output and Feature items, and reports that
 * have none of their own inputs, are not needed to read what the device sends.
 * Pure C: no QMK headers, so it builds and runs on the host for tests. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HID_DESC_DEVICES 8  /* one per TinyUSB HID instance (CFG_TUH_HID) */
#define HID_DESC_REPORTS 12 /* input reports per device */
#define HID_DESC_FIELDS 48  /* input fields per device */
#define HID_DESC_USAGES 64  /* listed usages per device */

/* Field flags, as in the Input item (HID 1.11 6.2.2.5). */
#define HID_FIELD_CONSTANT 0x01
#define HID_FIELD_VARIABLE 0x02
#define HID_FIELD_RELATIVE 0x04

/* What a top-level Application collection is, by its usage. */
typedef enum {
    HID_KIND_OTHER = 0,
    HID_KIND_KEYBOARD, /* Generic Desktop / Keyboard */
    HID_KIND_MOUSE,    /* Generic Desktop / Mouse */
    HID_KIND_CONSUMER, /* Consumer / Consumer Control */
    HID_KIND_SYSTEM,   /* Generic Desktop / System Control */
} hid_kind_t;

typedef struct {
    uint8_t  report;     /* index into hid_device_t.reports */
    uint8_t  flags;      /* HID_FIELD_* */
    uint16_t bit_offset; /* from the first byte after the Report ID */
    uint8_t  bit_size;   /* bits per element; values read at most 32 of them */
    uint16_t count;      /* Report Count */
    uint16_t usage_page;
    /* Usages, one of two forms:
     * - list_len > 0: listed usages usages[list_start .. list_start+list_len-1]
     *   (a Variable field gives element i the i-th one, the last one repeating;
     *   an Array field indexes them with value - logical_min).
     * - list_len == 0: the range usage_min..usage_max (a single Usage is a
     *   range of one). */
    uint16_t usage_min;
    uint16_t usage_max;
    uint8_t  list_start;
    uint8_t  list_len;
    int32_t  logical_min;
    int32_t  logical_max;
} hid_field_t;

typedef struct {
    uint8_t    id;   /* 0 when the device uses no Report IDs */
    hid_kind_t kind; /* of the Application collection the report is in */
    uint16_t   collection_page;
    uint16_t   collection_usage;
    uint16_t   bit_length; /* input payload, without the Report ID byte */
} hid_report_t;

typedef struct {
    bool         in_use;
    uint8_t      interface; /* caller's id for the HID interface */
    bool         uses_ids;
    uint8_t      report_count;
    uint8_t      field_count;
    uint8_t      usage_count;
    bool         truncated; /* a table filled up: some fields were left out */
    hid_report_t reports[HID_DESC_REPORTS];
    hid_field_t  fields[HID_DESC_FIELDS];
    uint16_t     usages[HID_DESC_USAGES];
} hid_device_t;

/* Compiles desc for interface, replacing what that interface had. False if
 * nothing usable was found (or no table entry was free); the interface then
 * has no device. */
bool hid_desc_compile(uint8_t interface, const uint8_t *desc, uint16_t len);
void hid_desc_remove(uint8_t interface);
void hid_desc_clear(void);

const hid_device_t *hid_desc_device(uint8_t interface);
/* The input report a received report belongs to: by its first byte when the
 * device uses Report IDs. NULL if unknown. */
const hid_report_t *hid_desc_find_report(const hid_device_t *dev, uint8_t id);
bool                hid_desc_has_kind(uint8_t interface, hid_kind_t kind);

/* Element i of a field as a signed value (sign-extended when Logical Minimum
 * is negative). Bits past len read as 0. */
int32_t hid_field_value(const hid_field_t *f, uint16_t i, const uint8_t *data, uint8_t len);
/* The usage element i of a Variable field stands for. */
uint16_t hid_field_variable_usage(const hid_device_t *dev, const hid_field_t *f, uint16_t i);
/* The usage an Array value selects; false for the null state (value outside
 * the logical range) or a value past the usages. */
bool hid_field_array_usage(const hid_device_t *dev, const hid_field_t *f, int32_t value, uint16_t *usage);

/* Iteration helper for the CLI and tests. */
const hid_device_t *hid_desc_device_at(uint8_t index);
