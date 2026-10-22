/* Host-test stand-in for TinyUSB's host HID API (tusb.h). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The board's tusb_config.h. */
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX 4
#define CFG_TUH_HID 8
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define HID_REPORT_TYPE_OUTPUT 2

bool tuh_hid_set_report(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, void *report,
                        uint16_t len);
bool tuh_hid_receive_report(uint8_t dev_addr, uint8_t idx);
