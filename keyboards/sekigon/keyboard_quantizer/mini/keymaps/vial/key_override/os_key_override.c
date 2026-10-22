// Copyright 2023 sekigon-gonnoc
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "os_key_override.h"

#include <stddef.h>

#include "vial.h"

/* The OS overrides live past Vial slot 32 in one shared NULL-terminated list.
 * Without Vial key overrides, key_overrides starts out NULL, slots 0..31 stay
 * NULL and process_key_override() stops at the first one, so nothing here would
 * ever run. Fail the build instead of silently doing nothing. */
#ifndef VIAL_KEY_OVERRIDE_ENABLE
#    error "os_key_override.c needs VIAL_KEY_OVERRIDE_ENABLE (KEY_OVERRIDE_ENABLE = yes)"
#endif

extern const key_override_t **key_overrides;
static uint8_t                os_override_cnt                                                        = 0;
static const key_override_t  *override_ptrs[VIAL_KEY_OVERRIDE_ENTRIES + OS_KEY_OVERRIDE_ENTRIES + 1] = {0};

void os_key_override_init(void) {
    /* Copy Vial's 32 slots once. Vial always fills them (disabled entries are
     * dummy structs, not NULL) and later reloads them in place, so the copied
     * pointers stay valid and process_key_override walks on into the OS
     * overrides at index 32. */
    if (key_overrides != NULL && key_overrides != override_ptrs) {
        for (size_t i = 0; i < VIAL_KEY_OVERRIDE_ENTRIES; ++i) {
            override_ptrs[i] = key_overrides[i];
        }
    }
    override_ptrs[VIAL_KEY_OVERRIDE_ENTRIES + os_override_cnt] = NULL;
    key_overrides                                              = override_ptrs;
}

void register_os_key_override(const key_override_t *override) {
    os_key_override_init();
    if (override == NULL || os_override_cnt >= OS_KEY_OVERRIDE_ENTRIES) {
        return;
    }
    override_ptrs[VIAL_KEY_OVERRIDE_ENTRIES + os_override_cnt] = override;
    os_override_cnt++;
    override_ptrs[VIAL_KEY_OVERRIDE_ENTRIES + os_override_cnt] = NULL;
}

void register_os_key_override_list(const key_override_t **list, uint8_t count) {
    remove_all_os_key_overrides();
    if (list == NULL) {
        return;
    }
    for (uint8_t i = 0; i < count; i++) {
        register_os_key_override(list[i]);
    }
}

void remove_all_os_key_overrides(void) {
    os_override_cnt                          = 0;
    override_ptrs[VIAL_KEY_OVERRIDE_ENTRIES] = NULL;
}
