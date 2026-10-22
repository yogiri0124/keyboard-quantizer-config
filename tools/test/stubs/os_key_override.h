/* Host-test stand-in for keymaps/vial/key_override/os_key_override.h. */
#pragma once

void os_key_override_init(void);
void register_us_key_on_jp_os_overrides(void);
void register_jp_key_on_us_os_overrides(void);
void remove_all_os_key_overrides(void);
