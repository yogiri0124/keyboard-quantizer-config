// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* The built-in default configuration (default.vil): written after every
 * reset of the saved configuration, see default_config.c. */

/* Writes default.vil's tap dances, combos, key overrides, macros and QMK
 * Settings; the layers come from keymaps[] with the reset itself. */
void default_config_write(void);
