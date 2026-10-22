/* Host-test stand-in for quantum/eeconfig.h. */
#pragma once

#include <stdint.h>

uint32_t eeconfig_read_user(void);
void     eeconfig_update_user(uint32_t value);
