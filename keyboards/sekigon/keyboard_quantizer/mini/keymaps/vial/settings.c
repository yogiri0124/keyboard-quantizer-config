// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include <string.h>

#include "eeconfig.h"
#include "via.h"

#include "settings.h"
#include "mouse.h"
#include "os_key_override.h"

/* The user EEPROM block. The layout is what earlier firmware saved: keep it. */
typedef union {
    uint32_t raw;
    struct {
        uint8_t os_override : 2;
        uint8_t reserved : 6;
        uint8_t scroll_div;
        uint8_t gesture_step;
        uint8_t cursor_scale : 6;
        uint8_t reserved2 : 2; /* once held test flags: ignored */
    };
} user_config_t;

_Static_assert(sizeof(user_config_t) == 4, "user_config_t must be 4 bytes");

typedef struct {
    const char *name;  /* CLI */
    const char *alias; /* CLI, or NULL */
    uint8_t     via_id;
    uint8_t     min;
    uint8_t     max;
    uint8_t     def;
    uint8_t     step;
} setting_def_t;

#define CURSOR_SCALE_MAX 32
_Static_assert(CURSOR_SCALE_MAX < 64, "cursor scale must fit its 6 bits");

/* VIA ids and ranges must match the vial.json Mouse menu. */
static const setting_def_t defs[SETTING_COUNT] = {
    [SETTING_SCROLL_DIV]   = {"scroll", NULL, 1, 8, 80, 32, 4},
    [SETTING_GESTURE_STEP] = {"thresh", "threshold", 2, 20, 120, 50, 10},
    [SETTING_CURSOR_SCALE] = {"dpi", NULL, 3, 4, CURSOR_SCALE_MAX, 16, 2},
};
#define VIA_ID_RESET 4

#define SAVE_DELAY_MS 750

static user_config_t config;
static uint8_t       values[SETTING_COUNT];
static bool          dirty;
static uint16_t      dirty_since;

static uint8_t clamp(setting_t s, int16_t v) {
    if (v < defs[s].min) {
        return defs[s].min;
    }
    if (v > defs[s].max) {
        return defs[s].max;
    }
    return (uint8_t)v;
}

static void save_now(void) {
    dirty               = false;
    config.reserved     = 0;
    config.reserved2    = 0;
    config.scroll_div   = values[SETTING_SCROLL_DIV];
    config.gesture_step = values[SETTING_GESTURE_STEP];
    config.cursor_scale = values[SETTING_CURSOR_SCALE] & 0x3F;
    eeconfig_update_user(config.raw);
}

/* A Vial slider sends one value per step while dragged; write once it
 * settles instead of on every step. */
static void save_later(void) {
    dirty       = true;
    dirty_since = timer_read();
}

void settings_task(void) {
    if (dirty && timer_elapsed(dirty_since) >= SAVE_DELAY_MS) {
        save_now();
    }
}

static void apply_os_override(uint8_t mode) {
    switch (mode) {
        case OS_OVERRIDE_US_ON_JP:
            register_us_key_on_jp_os_overrides();
            break;
        case OS_OVERRIDE_JP_ON_US:
            register_jp_key_on_us_os_overrides();
            break;
        default:
            remove_all_os_key_overrides();
            break;
    }
}

static void set_defaults(void) {
    for (uint8_t s = 0; s < SETTING_COUNT; s++) {
        values[s] = defs[s].def;
        ball_setting_changed((setting_t)s);
    }
}

void settings_init(void) {
    config.raw         = 0;
    config.os_override = OS_OVERRIDE_OFF;
    remove_all_os_key_overrides();
    set_defaults();
    save_now();
}

void settings_load(void) {
    config.raw    = eeconfig_read_user();
    bool repaired = false;
    if (config.os_override > OS_OVERRIDE_JP_ON_US) {
        config.os_override = OS_OVERRIDE_OFF;
        repaired           = true;
    }
    uint8_t saved[SETTING_COUNT] = {config.scroll_div, config.gesture_step, config.cursor_scale};
    for (uint8_t s = 0; s < SETTING_COUNT; s++) {
        bool ok   = saved[s] >= defs[s].min && saved[s] <= defs[s].max;
        values[s] = ok ? saved[s] : defs[s].def;
        repaired |= !ok;
        ball_setting_changed((setting_t)s);
    }
    if (repaired) {
        save_now();
    }
    apply_os_override(config.os_override);
}

void settings_reset(void) {
    set_defaults();
    save_now();
}

uint8_t settings_get(setting_t s) {
    return values[s];
}

uint8_t settings_min(setting_t s) {
    return defs[s].min;
}

uint8_t settings_max(setting_t s) {
    return defs[s].max;
}

const char *settings_name(setting_t s) {
    return defs[s].name;
}

void settings_set(setting_t s, uint8_t value) {
    uint8_t v = clamp(s, value);
    if (v != values[s]) {
        values[s] = v;
        ball_setting_changed(s);
    }
    save_later();
}

/* A smaller scroll divisor is faster and a smaller gesture step fires
 * sooner, so "faster" / "easier" keys pass -1 for those. */
void settings_step(setting_t s, int8_t dir) {
    settings_set(s, clamp(s, (int16_t)values[s] + (dir > 0 ? defs[s].step : -(int16_t)defs[s].step)));
}

bool settings_set_by_name(const char *name, uint8_t value) {
    if (name == NULL) {
        return false;
    }
    for (uint8_t s = 0; s < SETTING_COUNT; s++) {
        if (strcmp(name, defs[s].name) == 0 || (defs[s].alias != NULL && strcmp(name, defs[s].alias) == 0)) {
            settings_set((setting_t)s, value);
            return true;
        }
    }
    return false;
}

uint8_t os_override_get(void) {
    return config.os_override;
}

void os_override_set(uint8_t mode) {
    if (mode > OS_OVERRIDE_JP_ON_US) {
        mode = OS_OVERRIDE_OFF;
    }
    config.os_override = mode;
    apply_os_override(mode);
    save_now();
}

const char *os_override_name(uint8_t mode) {
    switch (mode) {
        case OS_OVERRIDE_US_ON_JP:
            return "uj";
        case OS_OVERRIDE_JP_ON_US:
            return "ju";
        default:
            return "off";
    }
}

/* The grave override differs between Windows and Mac, chosen from the
 * Alt/GUI swap when registered: register again when the swap changes. */
void os_override_refresh(void) {
    static int8_t last_swap = -1;
    int8_t        swap      = keymap_config.swap_lalt_lgui ? 1 : 0;
    if (last_swap >= 0 && swap != last_swap && config.os_override != OS_OVERRIDE_OFF) {
        apply_os_override(config.os_override);
    }
    last_swap = swap;
}

static bool setting_by_via_id(uint8_t id, setting_t *out) {
    for (uint8_t s = 0; s < SETTING_COUNT; s++) {
        if (defs[s].via_id == id) {
            *out = (setting_t)s;
            return true;
        }
    }
    return false;
}

/* data: command, channel, value id, value. */
void settings_via_command(uint8_t *data, uint8_t length) {
    if (data == NULL || length < 2) {
        return;
    }
    uint8_t *command = &data[0];
    if (data[1] != id_custom_channel) {
        *command = id_unhandled;
        return;
    }
    setting_t s;
    switch (*command) {
        case id_custom_set_value:
            if (length < 4) {
                break;
            }
            if (data[2] == VIA_ID_RESET) {
                mouse_reset_settings_and_latches();
            } else if (setting_by_via_id(data[2], &s)) {
                settings_set(s, data[3]);
            }
            break;
        case id_custom_get_value:
            if (length < 4) {
                break;
            }
            data[3] = setting_by_via_id(data[2], &s) ? values[s] : 0;
            break;
        case id_custom_save:
            save_now();
            break;
        default:
            *command = id_unhandled;
            break;
    }
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    settings_via_command(data, length);
}
