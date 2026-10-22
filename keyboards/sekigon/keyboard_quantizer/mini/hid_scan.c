// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_scan.h"

#include <string.h>

#define ITEM_LONG 0xFE
#define ITEM_USAGE_PAGE 0x04 /* prefix without its size bits */
#define ITEM_REPORT_ID 0x84
#define ITEM_OUTPUT 0x90
#define ITEM_PUSH 0xA4
#define ITEM_POP 0xB4
#define USAGE_PAGE_LED 0x08
#define STACK_DEPTH 4

void hid_scan(const uint8_t *desc, uint16_t len, hid_scan_t *out) {
    memset(out, 0, sizeof(*out));
    /* The two globals this needs, with Push / Pop. */
    uint16_t page = 0, page_stack[STACK_DEPTH];
    uint8_t  id = 0, id_stack[STACK_DEPTH], sp = 0;
    uint16_t pos = 0;
    while (desc != NULL && pos < len) {
        uint8_t prefix = desc[pos];
        if (prefix == ITEM_LONG) {
            if (pos + 1 >= len) {
                break;
            }
            pos = (uint16_t)(pos + 3 + desc[pos + 1]);
            continue;
        }
        uint8_t bytes = (uint8_t)(prefix & 0x03);
        bytes         = bytes == 3 ? 4 : bytes;
        if ((uint32_t)pos + 1 + bytes > len) {
            break;
        }
        uint32_t raw = 0;
        for (uint8_t i = 0; i < bytes; i++) {
            raw |= (uint32_t)desc[pos + 1 + i] << (8 * i);
        }
        switch (prefix & 0xFC) {
            case ITEM_USAGE_PAGE:
                page = (uint16_t)raw;
                break;
            case ITEM_REPORT_ID:
                id                   = (uint8_t)raw;
                out->uses_report_ids = true;
                break;
            case ITEM_PUSH:
                if (sp < STACK_DEPTH) {
                    page_stack[sp] = page;
                    id_stack[sp]   = id;
                    sp++;
                }
                break;
            case ITEM_POP:
                if (sp > 0) {
                    sp--;
                    page = page_stack[sp];
                    id   = id_stack[sp];
                }
                break;
            case ITEM_OUTPUT:
                if (page == USAGE_PAGE_LED && !out->has_led_output) {
                    out->has_led_output = true;
                    out->led_report_id  = id;
                }
                break;
            default:
                break;
        }
        pos = (uint16_t)(pos + 1 + bytes);
    }
}
