// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_input.h"

#include <string.h>

#include "hid_queue.h"

/* QMK's KC_MS_BTN1; matrix.c checks it against the real keycode. */
#define USAGE_MOUSE_BTN1 0xD1
#define MOUSE_MATRIX_BUTTONS 8 /* BTN1..8; more would collide with the wheel cells */
#define SOURCES_MAX 24

typedef struct {
    bool       used;
    uint8_t    interface;
    uint8_t    report_index;
    hid_kind_t kind;
    union {
        hid_keys_t       keys;
        uint16_t         buttons;
        hid_usage_list_t usages;
    };
} source_t;

static source_t          sources[SOURCES_MAX];
static hid_input_stats_t stats;
static uint16_t          sent_consumer;
static uint16_t          sent_system;

/* Sends to the host (weak defaults for the host tests; matrix.c has the
 * QMK ones). */
void kq_host_consumer(uint16_t usage);
void kq_host_system(uint16_t usage);

__attribute__((weak)) void kq_host_consumer(uint16_t usage) {
    (void)usage;
}
__attribute__((weak)) void kq_host_system(uint16_t usage) {
    (void)usage;
}
__attribute__((weak)) void kq_mouse_report(const hid_mouse_t *mouse) {
    (void)mouse;
}
__attribute__((weak)) void kq_mouse_scan_end(void) {}
__attribute__((weak)) void kq_mouse_device_lost(void) {}

/* Lenovo ThinkPad keyboard (17ef:6047): its TrackPoint scroll comes as a
 * vendor report whose first byte is horizontal scroll. */
__attribute__((weak)) void kq_vendor_report(uint16_t page, const uint8_t *data, uint8_t len) {
    if (page == 0xFFA1 && data != NULL && len > 0) {
        hid_mouse_t m = {0};
        m.pan         = (int8_t)data[0];
        kq_mouse_report(&m);
    }
}

hid_input_stats_t hid_input_stats(void) {
    return stats;
}

static source_t *find_source(uint8_t interface, uint8_t report_index, hid_kind_t kind, bool create) {
    source_t *spare = NULL;
    for (uint8_t i = 0; i < SOURCES_MAX; i++) {
        source_t *s = &sources[i];
        if (s->used && s->interface == interface && s->report_index == report_index) {
            return s;
        }
        if (!s->used && spare == NULL) {
            spare = s;
        }
    }
    if (!create || spare == NULL) {
        return NULL;
    }
    memset(spare, 0, sizeof(*spare));
    spare->used         = true;
    spare->interface    = interface;
    spare->report_index = report_index;
    spare->kind         = kind;
    return spare;
}

static void set_cell(uint8_t *matrix, uint16_t usage) {
    matrix[usage / 8 + 1] |= (uint8_t)(1u << (usage % 8));
}

static void compose(uint8_t *matrix) {
    memset(matrix, 0, KQ_MATRIX_ROWS);
    for (uint8_t i = 0; i < SOURCES_MAX; i++) {
        const source_t *s = &sources[i];
        if (!s->used) {
            continue;
        }
        if (s->kind == HID_KIND_KEYBOARD) {
            for (uint16_t u = 0; u < 0xF0; u++) {
                if (!((s->keys.bits[u >> 3] >> (u & 7)) & 1)) {
                    continue;
                }
                if (u >= 0xE0) {
                    matrix[KQ_ROW_MODIFIERS] |= (uint8_t)(1u << (u - 0xE0));
                } else {
                    set_cell(matrix, u);
                }
            }
        } else if (s->kind == HID_KIND_MOUSE) {
            for (uint8_t b = 0; b < MOUSE_MATRIX_BUTTONS; b++) {
                if ((s->buttons >> b) & 1) {
                    set_cell(matrix, (uint16_t)(USAGE_MOUSE_BTN1 + b));
                }
            }
        }
    }
}

/* The host gets one consumer and one system usage at a time: the first one
 * any source holds, 0 when none. Sent on change, so a held media key stays
 * held (and the host repeats it) and its release reaches the host. */
static uint16_t first_usage(hid_kind_t kind) {
    for (uint8_t i = 0; i < SOURCES_MAX; i++) {
        const source_t *s = &sources[i];
        if (s->used && s->kind == kind && s->usages.count > 0) {
            return s->usages.usages[0];
        }
    }
    return 0;
}

static void sync_host_usages(void) {
    uint16_t c = first_usage(HID_KIND_CONSUMER);
    if (c != sent_consumer) {
        sent_consumer = c;
        kq_host_consumer(c);
    }
    uint16_t s = first_usage(HID_KIND_SYSTEM);
    if (s != sent_system) {
        sent_system = s;
        kq_host_system(s);
    }
    stats.last_consumer = sent_consumer;
    stats.last_system   = sent_system;
}

static bool interface_has_mouse(uint8_t interface) {
    return hid_desc_has_kind(interface, HID_KIND_MOUSE);
}

/* The handlers return whether a key or button state changed, i.e. whether
 * the matrix needs composing again. */
static bool remove_interface(uint8_t interface) {
    bool had_mouse = interface_has_mouse(interface);
    bool changed   = false;
    for (uint8_t i = 0; i < SOURCES_MAX; i++) {
        if (sources[i].used && sources[i].interface == interface) {
            memset(&sources[i], 0, sizeof(sources[i]));
            changed = true;
        }
    }
    hid_desc_remove(interface);
    sync_host_usages();
    if (had_mouse) {
        kq_mouse_device_lost();
    }
    return changed;
}

static bool handle_report(uint8_t interface, const uint8_t *data, uint8_t len) {
    hid_decoded_t d;
    if (!hid_report_decode(interface, data, len, &d)) {
        if (hid_desc_device(interface) == NULL) {
            stats.unknown_reports++;
        }
        return false;
    }
    source_t *s;
    bool      changed = false;
    switch (d.kind) {
        case HID_KIND_KEYBOARD:
            stats.keyboard++;
            s = find_source(interface, d.report_index, d.kind, true);
            if (s != NULL && memcmp(&s->keys, &d.keys, sizeof(d.keys)) != 0) {
                s->keys = d.keys;
                changed = true;
            }
            break;
        case HID_KIND_MOUSE:
            stats.mouse++;
            if (d.mouse.has_buttons) {
                s = find_source(interface, d.report_index, d.kind, true);
                if (s != NULL && s->buttons != d.mouse.buttons) {
                    s->buttons = d.mouse.buttons;
                    changed    = true;
                }
                stats.last_buttons = d.mouse.buttons;
            }
            if (d.mouse.wheel != 0) {
                stats.last_wheel = d.mouse.wheel;
            }
            if (d.mouse.pan != 0) {
                stats.last_pan = d.mouse.pan;
            }
            stats.last_undefined = d.mouse.undefined;
            kq_mouse_report(&d.mouse);
            break;
        case HID_KIND_CONSUMER:
        case HID_KIND_SYSTEM:
            if (d.kind == HID_KIND_CONSUMER) {
                stats.consumer++;
            } else {
                stats.system++;
            }
            s = find_source(interface, d.report_index, d.kind, true);
            if (s != NULL) {
                s->usages = d.usages;
            }
            sync_host_usages();
            break;
        default:
            stats.other++;
            stats.last_other_page = d.collection_page;
            kq_vendor_report(d.collection_page, d.payload, d.payload_len);
            break;
    }
    return changed;
}

static bool handle_event(const hidq_event_t *ev) {
    bool changed = false;
    switch (ev->type) {
        case HIDQ_MOUNT:
            /* A remount on the same interface (or a reused one) starts clean. */
            changed = remove_interface(ev->interface);
            hid_desc_compile(ev->interface, ev->data, ev->len);
            break;
        case HIDQ_UNMOUNT:
            changed = remove_interface(ev->interface);
            break;
        case HIDQ_REPORT:
            changed = handle_report(ev->interface, ev->data, (uint8_t)ev->len);
            break;
    }
    return changed;
}

bool hid_input_scan(uint8_t *matrix) {
    uint8_t start[KQ_MATRIX_ROWS];
    memcpy(start, matrix, KQ_MATRIX_ROWS);
    hidq_event_t ev;
    bool         moved = false;
    while (!moved && hidq_pop(&ev)) {
        if (handle_event(&ev)) {
            compose(matrix);
            moved = memcmp(start, matrix, KQ_MATRIX_ROWS) != 0;
        }
    }
    kq_mouse_scan_end();
    return moved;
}
