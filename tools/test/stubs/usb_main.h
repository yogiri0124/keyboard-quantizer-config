/* Host-test stand-in for tmk_core/protocol/chibios/usb_main.h (and the
 * wheel multiplier of patches/0002). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define USB_ENDPOINT_IN_MOUSE 0
#define USB_ENDPOINT_IN_COUNT 1

typedef struct {
    int unused;
} usb_endpoint_in_t;

bool    usb_endpoint_in_is_inactive(usb_endpoint_in_t *endpoint);
uint8_t usb_mouse_wheel_multiplier(bool vertical);
