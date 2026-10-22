/* Just enough of QMK for host tests of firmware files that include it
 * (built with -DQMK_KEYBOARD_H='"qmk_stub.h"' and -I tools/test/stubs). The
 * functions are defined by the test, which records what was called. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TAP_CODE_DELAY 0
#define DYNAMIC_KEYMAP_MACRO_COUNT 16

void wait_ms(uint32_t ms);
