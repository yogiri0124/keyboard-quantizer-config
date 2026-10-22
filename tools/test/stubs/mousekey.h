/* Host-test stand-in for quantum/mousekey.h. */
#pragma once

#include <stdint.h>

#include "report.h"

extern uint8_t mk_wheel_delay;    /* in 10 ms */
extern uint8_t mk_wheel_interval; /* in ms */
report_mouse_t mousekey_get_report(void);
