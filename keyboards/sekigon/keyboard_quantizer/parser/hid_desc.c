// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_desc.h"

#include <string.h>

/* Item types and tags (HID 1.11 6.2.2). */
enum { ITEM_MAIN = 0, ITEM_GLOBAL = 1, ITEM_LOCAL = 2 };
enum {
    MAIN_INPUT          = 0x8,
    MAIN_OUTPUT         = 0x9,
    MAIN_COLLECTION     = 0xA,
    MAIN_FEATURE        = 0xB,
    MAIN_END_COLLECTION = 0xC,
};
enum {
    GLOBAL_USAGE_PAGE   = 0x0,
    GLOBAL_LOGICAL_MIN  = 0x1,
    GLOBAL_LOGICAL_MAX  = 0x2,
    GLOBAL_REPORT_SIZE  = 0x7,
    GLOBAL_REPORT_ID    = 0x8,
    GLOBAL_REPORT_COUNT = 0x9,
    GLOBAL_PUSH         = 0xA,
    GLOBAL_POP          = 0xB,
};
enum { LOCAL_USAGE = 0x0, LOCAL_USAGE_MIN = 0x1, LOCAL_USAGE_MAX = 0x2, LOCAL_DELIMITER = 0xA };

#define COLLECTION_APPLICATION 0x01
#define GLOBAL_STACK_DEPTH 4
#define LOCAL_USAGES_MAX 32

static hid_device_t devices[HID_DESC_DEVICES];

typedef struct {
    uint16_t usage_page;
    int32_t  logical_min;
    uint32_t logical_max_raw;
    uint8_t  logical_max_size; /* data bytes of the Logical Maximum item */
    uint8_t  report_size;
    uint16_t report_count;
    uint8_t  report_id;
} global_state_t;

/* A usage as written: a 4-byte Usage item carries its own page, a shorter one
 * takes the Usage Page in effect at the Main item, so a Usage Page written
 * after the usages still applies to them (as Linux reads it). */
typedef struct {
    uint16_t id;
    uint16_t page;
    bool     extended;
} local_usage_t;

typedef struct {
    local_usage_t usages[LOCAL_USAGES_MAX];
    uint8_t       n_usages;
    local_usage_t usage_min;
    local_usage_t usage_max;
    bool          has_min;
    bool          has_max;
    uint8_t       delimiter_depth;
    uint8_t       delimiter_sets; /* sets closed so far; only the first is used */
} local_state_t;

typedef struct {
    hid_device_t  *dev;
    global_state_t g;
    global_state_t stack[GLOBAL_STACK_DEPTH];
    uint8_t        sp;
    local_state_t  l;
    uint8_t        depth;
    hid_kind_t     app_kind;
    uint16_t       app_page;
    uint16_t       app_usage;
} compiler_t;

static int32_t sign_extend(uint32_t raw, uint8_t bytes) {
    if (bytes == 0 || bytes >= 4) {
        return (int32_t)raw;
    }
    uint32_t sign = (uint32_t)1 << (bytes * 8 - 1);
    return (raw & sign) ? (int32_t)(raw | ~((sign << 1) - 1)) : (int32_t)raw;
}

/* Logical Maximum is signed only when Logical Minimum is: 0..255 is commonly
 * written 15 00 25 FF, where FF is 255 (Linux hid-core decides the same). */
static int32_t logical_max(const global_state_t *g) {
    return g->logical_min < 0 ? sign_extend(g->logical_max_raw, g->logical_max_size) : (int32_t)g->logical_max_raw;
}

static hid_kind_t kind_of(uint16_t page, uint16_t usage) {
    if (page == 0x01 && usage == 0x06) {
        return HID_KIND_KEYBOARD;
    }
    if (page == 0x01 && usage == 0x02) {
        return HID_KIND_MOUSE;
    }
    if (page == 0x01 && usage == 0x80) {
        return HID_KIND_SYSTEM;
    }
    if (page == 0x0C && usage == 0x01) {
        return HID_KIND_CONSUMER;
    }
    return HID_KIND_OTHER;
}

static void reset_local(local_state_t *l) {
    memset(l, 0, sizeof(*l));
}

static local_usage_t make_usage(uint32_t raw, uint8_t bytes) {
    local_usage_t u = {(uint16_t)raw, (uint16_t)(raw >> 16), bytes == 4};
    return u;
}

static uint32_t resolve(const compiler_t *c, local_usage_t u) {
    uint16_t page = u.extended ? u.page : c->g.usage_page;
    return ((uint32_t)page << 16) | u.id;
}

static hid_report_t *report_for(compiler_t *c, uint8_t id) {
    hid_device_t *dev = c->dev;
    for (uint8_t i = 0; i < dev->report_count; i++) {
        if (dev->reports[i].id == id) {
            return &dev->reports[i];
        }
    }
    if (dev->report_count >= HID_DESC_REPORTS) {
        dev->truncated = true;
        return NULL;
    }
    hid_report_t *r     = &dev->reports[dev->report_count++];
    r->id               = id;
    r->kind             = c->app_kind;
    r->collection_page  = c->app_page;
    r->collection_usage = c->app_usage;
    r->bit_length       = 0;
    return r;
}

static void add_input(compiler_t *c, uint8_t flags) {
    hid_device_t *dev  = c->dev;
    uint8_t       size = c->g.report_size;
    uint16_t      cnt  = c->g.report_count;
    if (size == 0 || cnt == 0) {
        return; /* occupies nothing */
    }
    hid_report_t *r = report_for(c, c->g.report_id);
    if (r == NULL) {
        return;
    }
    if (c->g.report_id != 0) {
        dev->uses_ids = true;
    }
    uint32_t bits   = (uint32_t)size * cnt;
    uint16_t offset = r->bit_length;
    r->bit_length   = (uint16_t)(r->bit_length + bits > 0xFFFF ? 0xFFFF : r->bit_length + bits);

    if (dev->field_count >= HID_DESC_FIELDS) {
        dev->truncated = true;
        return;
    }
    hid_field_t *f = &dev->fields[dev->field_count];
    memset(f, 0, sizeof(*f));
    f->report      = (uint8_t)(r - dev->reports);
    f->flags       = flags;
    f->bit_offset  = offset;
    f->bit_size    = size;
    f->count       = cnt;
    f->logical_min = c->g.logical_min;
    f->logical_max = logical_max(&c->g);
    f->usage_page  = c->g.usage_page;

    const local_state_t *l = &c->l;
    if (l->n_usages > 0) {
        uint32_t first = resolve(c, l->usages[0]);
        f->usage_page  = (uint16_t)(first >> 16);
        bool run       = true;
        for (uint8_t i = 1; i < l->n_usages; i++) {
            if (resolve(c, l->usages[i]) != resolve(c, l->usages[i - 1]) + 1) {
                run = false;
            }
        }
        if (run) {
            /* Usages listed one by one in a consecutive run mean the same as
             * that range (09 E9 09 EA = Usage Minimum E9, Maximum EA), for a
             * Variable field (element i, the last repeating) and an Array
             * (value - Logical Minimum indexes them) alike. */
            f->usage_min = (uint16_t)first;
            f->usage_max = (uint16_t)resolve(c, l->usages[l->n_usages - 1]);
        } else if (dev->usage_count + l->n_usages <= HID_DESC_USAGES) {
            f->list_start = dev->usage_count;
            f->list_len   = l->n_usages;
            for (uint8_t i = 0; i < l->n_usages; i++) {
                dev->usages[dev->usage_count++] = (uint16_t)resolve(c, l->usages[i]);
            }
        } else {
            dev->truncated = true;
            f->usage_min = f->usage_max = (uint16_t)first;
        }
    } else if (l->has_min && l->has_max) {
        uint32_t lo   = resolve(c, l->usage_min);
        f->usage_page = (uint16_t)(lo >> 16);
        f->usage_min  = (uint16_t)lo;
        f->usage_max  = (uint16_t)resolve(c, l->usage_max);
    }
    dev->field_count++;
}

static void main_item(compiler_t *c, uint8_t tag, uint32_t raw) {
    switch (tag) {
        case MAIN_INPUT:
            add_input(c, (uint8_t)(raw & (HID_FIELD_CONSTANT | HID_FIELD_VARIABLE | HID_FIELD_RELATIVE)));
            break;
        case MAIN_COLLECTION:
            if (c->depth == 0) {
                /* The usage of a top-level collection names the device kind. */
                uint32_t u   = c->l.n_usages > 0 ? resolve(c, c->l.usages[0]) : ((uint32_t)c->g.usage_page << 16);
                c->app_page  = (uint16_t)(u >> 16);
                c->app_usage = (uint16_t)u;
                c->app_kind  = (raw & 0xFF) == COLLECTION_APPLICATION ? kind_of(c->app_page, c->app_usage) : HID_KIND_OTHER;
            }
            if (c->depth < 255) {
                c->depth++;
            }
            break;
        case MAIN_END_COLLECTION:
            if (c->depth > 0) {
                c->depth--;
            }
            if (c->depth == 0) {
                c->app_kind  = HID_KIND_OTHER;
                c->app_page  = 0;
                c->app_usage = 0;
            }
            break;
        default: /* Output, Feature */
            break;
    }
    /* Local items last until the next Main item (HID 1.11 6.2.2.8). */
    reset_local(&c->l);
}

static void global_item(compiler_t *c, uint8_t tag, uint32_t raw, uint8_t bytes) {
    global_state_t *g = &c->g;
    switch (tag) {
        case GLOBAL_USAGE_PAGE:
            g->usage_page = (uint16_t)raw;
            break;
        case GLOBAL_LOGICAL_MIN:
            g->logical_min = sign_extend(raw, bytes);
            break;
        case GLOBAL_LOGICAL_MAX:
            g->logical_max_raw  = raw;
            g->logical_max_size = bytes;
            break;
        case GLOBAL_REPORT_SIZE:
            g->report_size = raw > 255 ? 255 : (uint8_t)raw;
            break;
        case GLOBAL_REPORT_ID:
            g->report_id = raw > 255 ? 0 : (uint8_t)raw;
            break;
        case GLOBAL_REPORT_COUNT:
            g->report_count = raw > 0xFFFF ? 0xFFFF : (uint16_t)raw;
            break;
        case GLOBAL_PUSH:
            if (c->sp < GLOBAL_STACK_DEPTH) {
                c->stack[c->sp++] = *g;
            }
            break;
        case GLOBAL_POP:
            if (c->sp > 0) {
                *g = c->stack[--c->sp];
            }
            break;
        default: /* physical range, units */
            break;
    }
}

static void local_item(compiler_t *c, uint8_t tag, uint32_t raw, uint8_t bytes) {
    local_state_t *l = &c->l;
    if (tag == LOCAL_DELIMITER) {
        if (raw == 1) {
            l->delimiter_depth++;
        } else if (l->delimiter_depth > 0) {
            l->delimiter_depth--;
            l->delimiter_sets++;
        }
        return;
    }
    /* Only the first set of alternative usages counts. */
    if (l->delimiter_sets > 0 && l->delimiter_depth > 0) {
        return;
    }
    switch (tag) {
        case LOCAL_USAGE:
            if (l->n_usages < LOCAL_USAGES_MAX) {
                l->usages[l->n_usages++] = make_usage(raw, bytes);
            }
            break;
        case LOCAL_USAGE_MIN:
            l->usage_min = make_usage(raw, bytes);
            l->has_min   = true;
            break;
        case LOCAL_USAGE_MAX:
            l->usage_max = make_usage(raw, bytes);
            l->has_max   = true;
            break;
        default:
            break;
    }
}

static hid_device_t *free_device(void) {
    for (uint8_t i = 0; i < HID_DESC_DEVICES; i++) {
        if (!devices[i].in_use) {
            return &devices[i];
        }
    }
    return NULL;
}

bool hid_desc_compile(uint8_t interface, const uint8_t *desc, uint16_t len) {
    hid_desc_remove(interface);
    hid_device_t *dev = free_device();
    if (dev == NULL) {
        return false;
    }
    memset(dev, 0, sizeof(*dev));
    dev->interface = interface;

    compiler_t c;
    memset(&c, 0, sizeof(c));
    c.dev = dev;

    uint16_t pos = 0;
    while (desc != NULL && pos < len) {
        uint8_t prefix = desc[pos];
        if (prefix == 0xFE) {
            /* Long item: prefix, bDataSize, bLongItemTag, data. None of them
             * describe input fields. */
            if (pos + 2 >= len) {
                break;
            }
            pos = (uint16_t)(pos + 3 + desc[pos + 1]);
            continue;
        }
        uint8_t bytes = (uint8_t)(prefix & 0x03);
        if (bytes == 3) {
            bytes = 4;
        }
        if ((uint32_t)pos + 1 + bytes > len) {
            break; /* truncated item: stop, keep what was complete */
        }
        uint32_t raw = 0;
        for (uint8_t i = 0; i < bytes; i++) {
            raw |= (uint32_t)desc[pos + 1 + i] << (8 * i);
        }
        uint8_t type = (uint8_t)((prefix >> 2) & 0x03);
        uint8_t tag  = (uint8_t)(prefix >> 4);
        if (type == ITEM_MAIN) {
            main_item(&c, tag, raw);
        } else if (type == ITEM_GLOBAL) {
            global_item(&c, tag, raw, bytes);
        } else if (type == ITEM_LOCAL) {
            local_item(&c, tag, raw, bytes);
        }
        pos = (uint16_t)(pos + 1 + bytes);
    }

    if (dev->field_count == 0) {
        memset(dev, 0, sizeof(*dev));
        return false;
    }
    dev->in_use = true;
    return true;
}

void hid_desc_remove(uint8_t interface) {
    for (uint8_t i = 0; i < HID_DESC_DEVICES; i++) {
        if (devices[i].in_use && devices[i].interface == interface) {
            memset(&devices[i], 0, sizeof(devices[i]));
        }
    }
}

void hid_desc_clear(void) {
    memset(devices, 0, sizeof(devices));
}

const hid_device_t *hid_desc_device(uint8_t interface) {
    for (uint8_t i = 0; i < HID_DESC_DEVICES; i++) {
        if (devices[i].in_use && devices[i].interface == interface) {
            return &devices[i];
        }
    }
    return NULL;
}

const hid_device_t *hid_desc_device_at(uint8_t index) {
    if (index >= HID_DESC_DEVICES || !devices[index].in_use) {
        return NULL;
    }
    return &devices[index];
}

const hid_report_t *hid_desc_find_report(const hid_device_t *dev, uint8_t id) {
    if (dev == NULL) {
        return NULL;
    }
    for (uint8_t i = 0; i < dev->report_count; i++) {
        if (dev->reports[i].id == id) {
            return &dev->reports[i];
        }
    }
    return NULL;
}

bool hid_desc_has_kind(uint8_t interface, hid_kind_t kind) {
    const hid_device_t *dev = hid_desc_device(interface);
    if (dev == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < dev->report_count; i++) {
        if (dev->reports[i].kind == kind) {
            return true;
        }
    }
    return false;
}

int32_t hid_field_value(const hid_field_t *f, uint16_t i, const uint8_t *data, uint8_t len) {
    uint8_t  n     = f->bit_size > 32 ? 32 : f->bit_size;
    uint32_t start = (uint32_t)f->bit_offset + (uint32_t)i * f->bit_size;
    uint32_t v     = 0;
    for (uint8_t b = 0; b < n; b++) {
        uint32_t bit  = start + b;
        uint32_t byte = bit >> 3;
        if (byte < len && (data[byte] >> (bit & 7)) & 1) {
            v |= (uint32_t)1 << b;
        }
    }
    if (f->logical_min < 0 && n < 32 && (v >> (n - 1)) & 1) {
        v |= ~(((uint32_t)1 << n) - 1);
    }
    return (int32_t)v;
}

uint16_t hid_field_variable_usage(const hid_device_t *dev, const hid_field_t *f, uint16_t i) {
    if (f->list_len > 0) {
        uint16_t k = i < f->list_len ? i : (uint16_t)(f->list_len - 1);
        return dev->usages[f->list_start + k];
    }
    /* Past the range, the last usage applies to the rest (HID 1.11 6.2.2.8). */
    uint32_t u = (uint32_t)f->usage_min + i;
    return (uint16_t)(u > f->usage_max ? f->usage_max : u);
}

bool hid_field_array_usage(const hid_device_t *dev, const hid_field_t *f, int32_t value, uint16_t *usage) {
    if (value < f->logical_min || value > f->logical_max) {
        return false;
    }
    uint32_t idx = (uint32_t)(value - f->logical_min);
    if (f->list_len > 0) {
        if (idx >= f->list_len) {
            return false;
        }
        *usage = dev->usages[f->list_start + idx];
        return true;
    }
    uint32_t u = (uint32_t)f->usage_min + idx;
    if (u > f->usage_max) {
        return false;
    }
    *usage = (uint16_t)u;
    return true;
}
