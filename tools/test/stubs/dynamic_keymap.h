/* Host-test stand-in for quantum/dynamic_keymap.h. */
#pragma once

#include <stdint.h>

uint16_t dynamic_keymap_macro_get_buffer_size(void);
void     dynamic_keymap_macro_get_buffer(uint16_t offset, uint16_t size, uint8_t *data);
