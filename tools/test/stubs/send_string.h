/* Host-test stand-in for quantum/send_string/send_string.h. */
#pragma once

#include <stdint.h>

#include "qmk_stub.h" /* send_string() */

#define SS_QMK_PREFIX 1
#define SS_TAP_CODE 1
#define SS_DOWN_CODE 2
#define SS_UP_CODE 3
#define SS_DELAY_CODE 4

void send_string_with_delay(const char *string, uint8_t interval);
