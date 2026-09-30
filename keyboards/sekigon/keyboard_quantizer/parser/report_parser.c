// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#include "report_parser.h"
#include "report_descriptor_parser.h"

#include <stdint.h>

static void keyboard_report_parser(hid_report_member_t const *member,
                                   uint8_t const *data, uint8_t len);
static void mouse_report_parser(hid_report_member_t const *member, uint8_t const *data,
                                uint8_t len);
static void extra_key_report_parser(hid_report_member_t const *member, uint8_t const *data,
                                    uint8_t len, void (*hook)(uint16_t));

static bool hid_report_bit(uint8_t const *data, uint8_t len, uint16_t bit_idx) {
    uint16_t byte_idx = bit_idx / 8;
    if (byte_idx >= len) {
        return false;
    }
    return (data[byte_idx] & (1u << (bit_idx & 7))) != 0;
}

bool parse_report(uint8_t interface,
                  uint8_t const *report, uint8_t len) {
  hid_device_t const *device = get_hid_device_def(interface);

  if (device == NULL || report == NULL || len == 0) {
    return false;
  }

  hid_id_collection_t const *collection = device->id_collection;
  if (collection == NULL) {
    return false;
  }

  if (collection->id != 0) {
    while (collection != NULL && report[0] != collection->id) {
      collection = collection->next;
    }

    if (collection != NULL) {
      report = &report[1];
      len--;
    }
  }

  if (collection == NULL || len == 0) {
    return false;
  }

  hid_report_member_t const *member = collection->report_def_start;

  if (member == NULL) {
    return false;
  }

  uint16_t usage_id = (collection->usage_page << 8) | collection->usage;

  switch (usage_id) {
    case 0x0106:
      keyboard_report_parser(member, report, len);
      return true;
    case 0x0102:
      mouse_report_parser(member, report, len);
      return true;
    case 0x0180:
      extra_key_report_parser(member, report, len, system_report_hook);
      return true;
    case 0x0C01:
      extra_key_report_parser(member, report, len, consumer_report_hook);
      return true;
    default:
      vendor_report_parser(collection->usage_page, member, report, len);
      return false;
  }
}

static void keyboard_report_parser(hid_report_member_t const *member,
                            uint8_t const *data, uint8_t len) {
  keyboard_parse_result_t result = {0};
  uint16_t bit_idx = 0;

  while (member != NULL) {
    uint8_t  size  = member->global.report_size;
    uint8_t  count = member->global.report_count;
    uint16_t bits  = (uint16_t)size * count;

    if (member->global.usage_page == 0x07 && size > 0 && count > 0) {
      if (size == 1) {
        /* A Usage Minimum/Maximum range, or keys listed one Usage each (the
         * descriptor parser gives each listed usage its own member). */
        bool range = member->local.usage_maximum > member->local.usage_minimum || member->local.usage == 0;
        for (uint8_t idx = 0; idx < count; idx++) {
          if (bit_idx / 8 >= len) {
            break;
          }
          if (hid_report_bit(data, len, bit_idx)) {
            uint8_t keycode = (uint8_t)(range ? idx + member->local.usage_minimum : member->local.usage);
            result.bits[keycode >> 3] |= (uint8_t)(1u << (keycode & 7));
          }
          bit_idx++;
        }
      } else if (size == 8) {
        for (uint8_t idx = 0; idx < count; idx++) {
          uint8_t raw = 0;
          for (uint8_t b = 0; b < 8; b++) {
            if (bit_idx / 8 >= len) {
              break;
            }
            if (hid_report_bit(data, len, bit_idx)) {
              raw |= (uint8_t)(1u << b);
            }
            bit_idx++;
          }
          /* An array value is an index into the usage range:
           * usage = value - Logical Minimum + Usage Minimum. Values outside
           * the logical range mean "no key". */
          int32_t usage = (int32_t)raw - member->global.logical_minimum + member->local.usage_minimum;
          if (raw >= member->global.logical_minimum && raw <= member->global.logical_maximum && usage > 0 &&
              usage <= 0xFF) {
            uint8_t keycode = (uint8_t)usage;
            result.bits[keycode >> 3] |= (uint8_t)(1u << (keycode & 7));
          }
        }
      } else {
        bit_idx += bits;
      }
    } else {
      /* Constant padding and other pages still occupy bits in the report. */
      bit_idx += bits;
    }

    member = member->next;
  }

  keyboard_report_hook(&result);
}

bool report_parser_boot_keyboard(uint8_t const *report, uint8_t report_len) {
    const uint8_t BOOT_KEYBOARD_REPORT_LEN     = 8;
    const uint8_t BOOT_KEYBOARD_REPORT_KEY_LEN = 6;

    if (report_len == BOOT_KEYBOARD_REPORT_LEN) {
        keyboard_parse_result_t result = {0};

        for (int bit = 0; bit < 8; bit++) {
            uint8_t mod_bit = (report[0] & (1 << bit));
            if (mod_bit != 0) {
                uint8_t keycode = bit + 0xe0;
                result.bits[(keycode >> 3)] |= (1 << (keycode & 0x07));
            }
        }

        for (int idx = 0; idx < BOOT_KEYBOARD_REPORT_KEY_LEN; idx++) {
            uint8_t keycode = report[idx + 2];
            if (keycode != 0) {
                result.bits[(keycode >> 3)] |= (1 << (keycode & 0x07));
            }
        }

        keyboard_report_hook(&result);

        return true;
    }

    return false;
}

static int16_t parse_value(hid_report_member_t const *member, uint8_t const *data,
                    uint16_t *bit_idx, uint8_t len) {
  uint32_t result = 0;
  /* report_size * report_count reaches 504 bits on this device, so a uint8_t
   * here would wrap and fold high bits back onto the low ones. */
  uint16_t offset = 0;

  if (member != NULL) {
    for (uint8_t idx = 0; idx < member->global.report_count; idx++) {
      for (uint8_t dst_bit_idx = 0; dst_bit_idx < member->global.report_size;
           dst_bit_idx++) {
        if (hid_report_bit(data, len, *bit_idx) && offset < 32) {
          result |= (1u << offset);
        }
        offset++;
        (*bit_idx)++;
      }
    }

    if (member->global.report_size != 1) {
      uint8_t bits = member->global.report_size;
      int32_t signed_result = (int32_t)result;
      /* Sign-extend only when Logical Minimum is negative. Unsigned fields
       * (buttons, some 8-bit axes) must keep the high bit as magnitude. */
      if (member->global.logical_minimum < 0 && bits > 0 && bits < 32 &&
          (result & (1u << (bits - 1)))) {
        signed_result |= (int32_t)(~((1u << bits) - 1u));
      }

      if (member->local.usage_maximum > member->local.usage_minimum) {
        signed_result += member->local.usage_minimum - member->global.logical_minimum;
      }
      if (signed_result < INT16_MIN) {
        signed_result = INT16_MIN;
      }
      if (signed_result > INT16_MAX) {
        signed_result = INT16_MAX;
      }
      return (int16_t)signed_result;
    }
  }

  return (int16_t)result;
}

static void mouse_report_parser(hid_report_member_t const *member, uint8_t const *data,
                         uint8_t len) {
  mouse_parse_result_t result = {0};

  uint16_t bit_idx = 0;
  while (member != NULL) {
    uint32_t usage_id =
        (((uint32_t)member->global.usage_page) << 16) | member->local.usage;

    uint16_t bit_idx_cur = bit_idx;
    int16_t val = parse_value(member, data, &bit_idx, len);

    switch (usage_id) {
    case 0x00090000 ... 0x00090010:
      /* Packed usage 0 includes ELECOM HUGE PLUS's 3-bit Constant field after
       * 5 declared buttons; those bits are buttons 6-8. Do not strip Constant. */
      result.has_button = true;
      result.button |= (uint16_t)((uint16_t)val << (bit_idx_cur & 0x07));
      break;

    case 0x00010030: // Generic, X
      result.x = val;
      break;

    case 0x00010031: // Generic, Y
      result.y = val;
      break;

    case 0x00010038: // Generic, Wheel
      result.v = val;
      break;

    case 0x000C0238: // Consumer, AC Pan
      result.h = val;
      break;

    case 0xFF000000: // SlimBlade, Button 3,4
      result.has_button = true;
      result.button |= (uint16_t)((uint16_t)val << ((bit_idx_cur + 2) & 0x0F));
      break;

    default:
      result.undefined |= (uint16_t)((uint16_t)val << (bit_idx_cur & 0x0F));
      break;
    }

    member = member->next;
  }

  mouse_report_hook(&result);
}

static uint32_t read_bits(uint8_t const *data, uint8_t len, uint16_t *bit_idx, uint8_t size) {
  uint32_t value = 0;
  for (uint8_t b = 0; b < size; b++) {
    if (hid_report_bit(data, len, *bit_idx) && b < 32) {
      value |= (1u << b);
    }
    (*bit_idx)++;
  }
  return value;
}

static void extra_key_report_parser(hid_report_member_t const *member, uint8_t const *data,
                                    uint8_t len, void (*hook)(uint16_t)) {
  uint16_t bit_idx = 0;

  if (hook == NULL) {
    return;
  }
  while (member != NULL) {
    /* Bitmap form: report_size 1 over a Usage Minimum..Maximum range, one bit
     * per usage. Expand it instead of packing every bit into one value. */
    if (member->global.report_size == 1 && member->global.report_count > 1 &&
        member->local.usage_maximum > member->local.usage_minimum) {
      for (uint8_t idx = 0; idx < member->global.report_count; idx++) {
        if (hid_report_bit(data, len, bit_idx)) {
          hook((uint16_t)(member->local.usage_minimum + idx));
        }
        bit_idx++;
      }
      member = member->next;
      continue;
    }

    /* Array form: every element is its own usage index (two keys held give
     * two elements); parse_value() would pack them all into one number. */
    if (member->global.report_size > 1 && member->global.report_count > 1) {
      for (uint8_t idx = 0; idx < member->global.report_count; idx++) {
        int32_t raw   = (int32_t)read_bits(data, len, &bit_idx, member->global.report_size);
        int32_t usage = raw;
        if (member->local.usage_maximum > member->local.usage_minimum) {
          usage = raw - member->global.logical_minimum + member->local.usage_minimum;
        }
        if (raw >= member->global.logical_minimum && raw <= member->global.logical_maximum && usage > 0 &&
            usage <= 0xFFFF) {
          hook((uint16_t)usage);
        }
      }
      member = member->next;
      continue;
    }

    uint16_t result = parse_value(member, data, &bit_idx, len);
    if (result != 0) {
      hook(member->global.report_size == 1 ? member->local.usage : result);
    }
    member = member->next;
  }
}
