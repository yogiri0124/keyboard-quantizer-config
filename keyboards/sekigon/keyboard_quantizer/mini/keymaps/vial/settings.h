// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Saved settings: the three ball values and the JP/US key override, in the
 * 4-byte user EEPROM block. One table describes each value (range, default,
 * step, VIA id, CLI name), and the VIA menu, the CLI and the SCR+/DPI-/THR+
 * keys all go through it. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SETTING_SCROLL_DIV = 0, /* ball counts per scroll detent: smaller is faster */
    SETTING_GESTURE_STEP,   /* ball counts per gesture tap: smaller fires sooner and more */
    SETTING_CURSOR_SCALE,   /* cursor speed in 1/16: 16 = 1:1 */
    SETTING_COUNT,
} setting_t;

enum {
    OS_OVERRIDE_OFF = 0,
    OS_OVERRIDE_US_ON_JP, /* "uj": a US keyboard on a JP OS */
    OS_OVERRIDE_JP_ON_US, /* "ju": a JP keyboard on a US OS */
};

void settings_load(void);  /* after EEPROM is up */
void settings_init(void);  /* fresh EEPROM: defaults, saved */
void settings_reset(void); /* RST: the three values back to default, saved */
void settings_task(void);  /* writes changed values once they settle */

uint8_t     settings_get(setting_t s);
void        settings_set(setting_t s, uint8_t value); /* clamped; saved a little later */
void        settings_step(setting_t s, int8_t dir);   /* one keycode step */
uint8_t     settings_min(setting_t s);
uint8_t     settings_max(setting_t s);
const char *settings_name(setting_t s);
bool        settings_set_by_name(const char *name, uint8_t value);

uint8_t     os_override_get(void);
void        os_override_set(uint8_t mode); /* applied and saved */
const char *os_override_name(uint8_t mode);
void        os_override_refresh(void); /* re-apply if the Alt/GUI swap changed */

/* VIA custom values (vial.json Mouse menu). */
void settings_via_command(uint8_t *data, uint8_t length);

/* Called when a value changes (mouse.c drops state that depended on it). */
void mouse_setting_changed(setting_t s);
