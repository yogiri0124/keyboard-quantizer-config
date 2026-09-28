// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "action.h" // keyrecord_t

void mouse_config_load(void);
void mouse_config_reset(void);
void mouse_config_save(void);
void mouse_housekeeping(void);
bool process_record_mouse(uint16_t keycode, keyrecord_t *record);
void post_process_record_mouse(uint16_t keycode, keyrecord_t *record);

void mouse_cli_print(void);
/* name is a CLI tunable ("scroll" / "dpi" / "thresh"); false if unknown. */
bool mouse_set_named_value(const char *name, uint8_t value);

void via_custom_value_command_kb(uint8_t *data, uint8_t length);

void hid_cli_print(void);
void hid_cli_print_desc(void);
