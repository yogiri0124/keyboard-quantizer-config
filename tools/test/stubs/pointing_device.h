/* Host-test stand-in for quantum/pointing_device/pointing_device.h, with
 * MOUSE_EXTENDED_REPORT. */
#pragma once

#include <stdint.h>

#define XY_REPORT_MIN INT16_MIN
#define XY_REPORT_MAX INT16_MAX
typedef int16_t mouse_xy_report_t;

#include <stdbool.h>

bool pointing_device_task(void);
void pointing_device_keycode_handler(uint16_t keycode, bool pressed);
