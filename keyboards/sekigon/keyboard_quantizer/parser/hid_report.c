// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_report.h"

#include <string.h>

#define PAGE_GENERIC_DESKTOP 0x01
#define PAGE_KEYBOARD 0x07
#define PAGE_BUTTON 0x09
#define PAGE_CONSUMER 0x0C
#define PAGE_SLIMBLADE 0xFF00

#define USAGE_X 0x30
#define USAGE_Y 0x31
#define USAGE_WHEEL 0x38
#define USAGE_AC_PAN 0x0238

#define KEY_ERROR_ROLLOVER 0x01
#define KEY_ERROR_UNDEFINED 0x03

static bool is_variable(const hid_field_t *f) {
    return (f->flags & HID_FIELD_VARIABLE) != 0;
}

static bool is_constant(const hid_field_t *f) {
    return (f->flags & HID_FIELD_CONSTANT) != 0;
}

static bool has_range(const hid_field_t *f) {
    return f->list_len == 0 && f->usage_max > f->usage_min;
}

static int16_t clamp16(int32_t v) {
    return (int16_t)(v > INT16_MAX ? INT16_MAX : (v < INT16_MIN ? INT16_MIN : v));
}

/* Every bit of a field (all elements) as one unsigned number, low bit first,
 * at most 32 bits. Used where a field is a plain bit pattern. */
static uint32_t field_bits(const hid_field_t *f, const uint8_t *data, uint8_t len) {
    uint32_t total = (uint32_t)f->bit_size * f->count;
    uint32_t v     = 0;
    for (uint32_t b = 0; b < total && b < 32; b++) {
        uint32_t bit  = f->bit_offset + b;
        uint32_t byte = bit >> 3;
        if (byte < len && (data[byte] >> (bit & 7)) & 1) {
            v |= (uint32_t)1 << b;
        }
    }
    return v;
}

static void set_key(hid_keys_t *k, uint16_t usage) {
    /* 1..3 are error codes, not keys. */
    if (usage > KEY_ERROR_UNDEFINED && usage <= 0xFF) {
        k->bits[usage >> 3] |= (uint8_t)(1u << (usage & 7));
    }
}

/* False when the keyboard reports a rollover error: the report then carries
 * no key state and the previous one stands (HID 1.11 appendix C). */
static bool decode_keyboard(const hid_device_t *dev, uint8_t report, const uint8_t *data, uint8_t len, hid_keys_t *k) {
    memset(k, 0, sizeof(*k));
    for (uint8_t fi = 0; fi < dev->field_count; fi++) {
        const hid_field_t *f = &dev->fields[fi];
        if (f->report != report || is_constant(f) || f->usage_page != PAGE_KEYBOARD) {
            continue;
        }
        for (uint16_t i = 0; i < f->count; i++) {
            int32_t value = hid_field_value(f, i, data, len);
            if (is_variable(f)) {
                if (value != 0) {
                    set_key(k, hid_field_variable_usage(dev, f, i));
                }
                continue;
            }
            uint16_t usage;
            if (hid_field_array_usage(dev, f, value, &usage)) {
                if (usage == KEY_ERROR_ROLLOVER) {
                    return false;
                }
                set_key(k, usage);
            }
        }
    }
    return true;
}

static void decode_mouse(const hid_device_t *dev, uint8_t report, const uint8_t *data, uint8_t len, hid_mouse_t *m) {
    memset(m, 0, sizeof(*m));
    for (uint8_t fi = 0; fi < dev->field_count; fi++) {
        const hid_field_t *f = &dev->fields[fi];
        if (f->report != report) {
            continue;
        }
        if (f->usage_page == PAGE_BUTTON) {
            m->has_buttons = true;
            if (!is_constant(f) && is_variable(f) && f->bit_size == 1) {
                for (uint16_t i = 0; i < f->count; i++) {
                    uint16_t usage = hid_field_variable_usage(dev, f, i);
                    if (usage >= 1 && usage <= 16 && hid_field_value(f, i, data, len) != 0) {
                        m->buttons |= (uint16_t)(1u << (usage - 1));
                    }
                }
            } else if (!is_constant(f) && !is_variable(f)) {
                for (uint16_t i = 0; i < f->count; i++) {
                    uint16_t usage;
                    if (hid_field_array_usage(dev, f, hid_field_value(f, i, data, len), &usage) && usage >= 1 &&
                        usage <= 16) {
                        m->buttons |= (uint16_t)(1u << (usage - 1));
                    }
                }
            } else {
                /* Constant (or packed) bits in the button page are buttons by
                 * position: the ELECOM HUGE PLUS declares 5 buttons and sends
                 * buttons 6-8 in the 3 "padding" bits after them. */
                m->buttons |= (uint16_t)(field_bits(f, data, len) << (f->bit_offset & 0x07));
            }
            continue;
        }
        if (f->usage_page == PAGE_SLIMBLADE && f->list_len == 0 && f->usage_min == 0) {
            /* Kensington SlimBlade: buttons 3 and 4 in a vendor field. */
            m->has_buttons = true;
            m->buttons |= (uint16_t)(field_bits(f, data, len) << ((f->bit_offset + 2) & 0x0F));
            continue;
        }
        if (is_constant(f)) {
            continue;
        }
        bool known = false;
        for (uint16_t i = 0; i < f->count && is_variable(f); i++) {
            uint16_t usage = hid_field_variable_usage(dev, f, i);
            int16_t  value = clamp16(hid_field_value(f, i, data, len));
            if (f->usage_page == PAGE_GENERIC_DESKTOP && usage == USAGE_X) {
                m->x  = value;
                known = true;
            } else if (f->usage_page == PAGE_GENERIC_DESKTOP && usage == USAGE_Y) {
                m->y  = value;
                known = true;
            } else if (f->usage_page == PAGE_GENERIC_DESKTOP && usage == USAGE_WHEEL) {
                m->wheel = value;
                known    = true;
            } else if (f->usage_page == PAGE_CONSUMER && usage == USAGE_AC_PAN) {
                m->pan = value;
                known  = true;
            }
        }
        if (!known) {
            m->undefined |= (uint16_t)(field_bits(f, data, len) << (f->bit_offset & 0x0F));
        }
    }
}

static void add_usage(hid_usage_list_t *u, uint16_t usage) {
    if (usage == 0 || u->count >= HID_USAGE_LIST_MAX) {
        return;
    }
    for (uint8_t i = 0; i < u->count; i++) {
        if (u->usages[i] == usage) {
            return;
        }
    }
    u->usages[u->count++] = usage;
}

static void decode_usages(const hid_device_t *dev, uint8_t report, uint16_t page, const uint8_t *data, uint8_t len,
                          hid_usage_list_t *u) {
    memset(u, 0, sizeof(*u));
    for (uint8_t fi = 0; fi < dev->field_count; fi++) {
        const hid_field_t *f = &dev->fields[fi];
        if (f->report != report || is_constant(f) || f->usage_page != page) {
            continue;
        }
        for (uint16_t i = 0; i < f->count; i++) {
            int32_t value = hid_field_value(f, i, data, len);
            if (is_variable(f) && f->bit_size == 1) {
                if (value != 0) {
                    add_usage(u, hid_field_variable_usage(dev, f, i));
                }
            } else if (!is_variable(f) || has_range(f)) {
                /* An array, or a multi-bit field over a usage range used as
                 * one: the value selects the usage; out of range is "none". */
                uint16_t usage;
                if (hid_field_array_usage(dev, f, value, &usage)) {
                    add_usage(u, usage);
                }
            } else if (value > 0 && value <= 0xFFFF) {
                /* A multi-bit Variable with no range: the value is the usage. */
                add_usage(u, (uint16_t)value);
            }
        }
    }
}

bool hid_report_decode(uint8_t interface, const uint8_t *data, uint8_t len, hid_decoded_t *out) {
    const hid_device_t *dev = hid_desc_device(interface);
    if (dev == NULL || data == NULL || len == 0 || out == NULL) {
        return false;
    }
    uint8_t id = 0;
    if (dev->uses_ids) {
        id = data[0];
        data++;
        len--;
    }
    const hid_report_t *r = hid_desc_find_report(dev, id);
    if (r == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    uint8_t ri           = (uint8_t)(r - dev->reports);
    out->kind            = r->kind;
    out->report_index    = ri;
    out->report_id       = id;
    out->collection_page = r->collection_page;
    out->payload         = data;
    out->payload_len     = len;
    switch (r->kind) {
        case HID_KIND_KEYBOARD:
            return decode_keyboard(dev, ri, data, len, &out->keys);
        case HID_KIND_MOUSE:
            decode_mouse(dev, ri, data, len, &out->mouse);
            return true;
        case HID_KIND_CONSUMER:
            decode_usages(dev, ri, PAGE_CONSUMER, data, len, &out->usages);
            return true;
        case HID_KIND_SYSTEM:
            decode_usages(dev, ri, PAGE_GENERIC_DESKTOP, data, len, &out->usages);
            return true;
        case HID_KIND_OTHER:
            return true; /* the caller gets the payload */
    }
    return false;
}
