// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "action.h"

bool process_record_mouse(uint16_t keycode, keyrecord_t *record);
void post_process_record_mouse(uint16_t keycode, keyrecord_t *record);
