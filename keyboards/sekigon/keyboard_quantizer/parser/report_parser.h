// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t bits[256 / 8];
} keyboard_parse_result_t;

typedef struct {
    uint16_t button;
    bool     has_button;
    int16_t  x;
    int16_t  y;
    int16_t  v;
    int16_t  h;
    uint16_t undefined;
} mouse_parse_result_t;

bool parse_report(uint8_t interface, uint8_t const *report, uint8_t len);
bool report_parser_boot_keyboard(uint8_t const *report, uint8_t report_len);

/* Default hooks live in mini/matrix.c; keymap may override. */
void keyboard_report_hook(keyboard_parse_result_t const *report);
void mouse_report_hook(mouse_parse_result_t const *report);
void system_report_hook(uint16_t report);
void consumer_report_hook(uint16_t report);
void keyboard_report_post_hook(void);
void mouse_on_host_disconnect(void);
void mouse_scan_end(void);
void mouse_after_send(void);
void hid_note_consumer(uint16_t report);
void hid_note_system(uint16_t report);
void hid_note_vendor(uint16_t usage_page);

