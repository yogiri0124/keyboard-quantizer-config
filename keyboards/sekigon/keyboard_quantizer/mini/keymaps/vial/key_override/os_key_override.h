// Copyright 2023 sekigon-gonnoc
/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <stdint.h>

#include "process_key_override.h"

#define OS_KEY_OVERRIDE_ENTRIES 25
#define OS_OVERRIDE_LEN(arr) ((uint8_t)(sizeof(arr) / sizeof((arr)[0])))

// helper macro
#define w_shift(kc, kc_override) &ko_make_basic(MOD_MASK_SHIFT, kc, kc_override)
#define wo_shift(kc, kc_override)                            \
    &ko_make_with_layers_and_negmods(0, kc, kc_override, ~0, \
                                     (uint8_t)MOD_MASK_SHIFT)

int register_os_key_override(const key_override_t *override);
void register_os_key_override_list(const key_override_t **list, uint8_t count);
void remove_all_os_key_overrides(void);

void os_key_override_init(void);
void register_us_key_on_jp_os_overrides(void);
void register_jp_key_on_us_os_overrides(void);