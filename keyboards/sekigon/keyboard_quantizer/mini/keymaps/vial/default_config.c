// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* The default configuration: default.vil, the layout Vial saves, built in.
 *
 * default_config.h is generated from default.vil at build time
 * (tools/vil_to_c.py, run by CI). It holds QMK's keymaps[] (the default
 * layers) and the tap dances, combos, key overrides, macros and QMK
 * Settings, which are written here whenever the saved configuration is reset
 * (a new firmware with a different EEPROM layout, or a reset from Vial). So
 * the device comes up configured, with nothing to load in Vial. */

#include QMK_KEYBOARD_H

#include "via.h"
#include "vial.h"
#include "dynamic_keymap.h"
#include "qmk_settings.h"

#define KQ_DEFAULT_DATA
#include "default_config.h"

_Static_assert(KQ_DEFAULT_TAP_DANCE_COUNT <= VIAL_TAP_DANCE_ENTRIES, "default.vil has too many tap dances");
_Static_assert(KQ_DEFAULT_COMBO_COUNT <= VIAL_COMBO_ENTRIES, "default.vil has too many combos");
_Static_assert(KQ_DEFAULT_KEY_OVERRIDE_COUNT <= VIAL_KEY_OVERRIDE_ENTRIES, "default.vil has too many key overrides");

/* QMK calls this first thing in via_init(), before it checks whether the
 * saved configuration belongs to this firmware. When it does not, QMK would
 * reset it to the bare keymap; do that here instead and add the rest. */
void via_init_kb(void) {
    if (via_eeprom_is_valid()) {
        return;
    }
    /* Layers from keymaps[] (default.vil), everything else emptied; marks
     * the configuration valid. */
    eeconfig_init_via();
    for (uint8_t i = 0; i < KQ_DEFAULT_TAP_DANCE_COUNT; i++) {
        dynamic_keymap_set_tap_dance(i, &kq_default_tap_dance[i]);
    }
    for (uint8_t i = 0; i < KQ_DEFAULT_COMBO_COUNT; i++) {
        dynamic_keymap_set_combo(i, &kq_default_combos[i]);
    }
    for (uint8_t i = 0; i < KQ_DEFAULT_KEY_OVERRIDE_COUNT; i++) {
        dynamic_keymap_set_key_override(i, &kq_default_key_overrides[i]);
    }
    dynamic_keymap_macro_set_buffer(0, sizeof(kq_default_macros), (uint8_t *)kq_default_macros);
    for (uint8_t i = 0; i < sizeof(kq_default_settings) / sizeof(kq_default_settings[0]); i++) {
        uint32_t value = kq_default_settings[i].value; /* setters take the low bytes they need */
        qmk_settings_set(kq_default_settings[i].qsid, &value, sizeof(value));
    }
    /* Vial loaded tap dances, combos and key overrides before this ran. */
    vial_init();
}
