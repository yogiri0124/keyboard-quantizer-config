/* Host-test stand-in for TinyUSB's host HID API (tusb.h). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HID_REPORT_TYPE_OUTPUT 2

bool tuh_hid_set_report(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, void *report,
                        uint16_t len);
bool tuh_hid_receive_report(uint8_t dev_addr, uint8_t idx);
