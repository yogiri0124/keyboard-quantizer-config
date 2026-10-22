/* Just enough of QMK for host tests of firmware files that include it
 * (built with -DQMK_KEYBOARD_H='"qmk_stub.h"' and -I tools/test/stubs).
 * Values are QMK's own; the functions are defined by each test, which
 * records what was called. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "action.h"
#include "keycodes.h"

#define TAP_CODE_DELAY 0
#define DYNAMIC_KEYMAP_MACRO_COUNT 16
#define MATRIX_ROWS 32
#define MATRIX_COLS 8

void wait_ms(uint32_t ms);

/* quantum/action_util.h */
typedef enum { ONESHOT_PRESSED = 1, ONESHOT_OTHER_KEY_PRESSED = 2 } oneshot_fullfillment_t;
void clear_oneshot_layer_state(oneshot_fullfillment_t state);
void clear_oneshot_mods(void);
