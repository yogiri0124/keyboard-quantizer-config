// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t usage;
  int32_t usage_maximum;
  int32_t usage_minimum;
} hid_report_local_member_t;

typedef struct struct_hid_report_member {
  struct {
    uint16_t usage_page;
    /* Logical Minimum / Maximum are signed and may be four bytes wide. */
    int32_t logical_maximum;
    int32_t logical_minimum;
    uint8_t report_size;
    uint8_t report_count;
    /* Report ID is a Global item too (HID 1.11 6.2.2.7): it may come before
     * the Collection it applies to, and Push/Pop save and restore it. */
    uint8_t report_id;
  } global;

  hid_report_local_member_t local;

  /* Explicit allocation flag. Deriving "free" from a payload field (report_size
   * was used before) lets a stored member look free and be handed out twice,
   * which self-links the list and hangs the report walk. */
  bool in_use;

  struct struct_hid_report_member *next;
} hid_report_member_t;

typedef struct {
  uint8_t prefix;
  uint8_t tag;
  uint8_t size;     /* short item: data byte count (0, 1, 2 or 4) */
  int32_t data;     /* raw, sign extended from size */
  uint32_t raw;     /* as encoded, unsigned */
} hid_report_item_t;

typedef struct struct_hid_id_collection {
  uint8_t id;
  uint16_t usage_page;
  uint16_t usage;
  hid_report_member_t *report_def_start;
  struct struct_hid_id_collection *next;
} hid_id_collection_t;

typedef struct {
  uint8_t interface;
  hid_id_collection_t *id_collection;
} hid_device_t;

#define HID_DEVICE_COUNT 8
#define HID_ID_COLLECTION_COUNT 16
#define HID_REPORT_MEMBER_COUNT 32
#define HID_USAGE_COUNT 32

bool parse_report_descriptor(uint8_t interface,
                             uint8_t const *desc, uint16_t len);
void delete_hid_device(uint8_t interface);
void print_hid_device(uint8_t interface);
void print_hid_devices_cli(void);
bool hid_has_mouse(void);
bool hid_interface_is_mouse(uint8_t interface);
bool hid_interface_is_keyboard(uint8_t interface);

hid_device_t const * get_hid_device_def(uint8_t interface);

void vendor_report_parser(uint16_t usage_page, hid_report_member_t const *member,
                          uint8_t const *data, uint8_t len);
