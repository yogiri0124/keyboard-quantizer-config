VIA_ENABLE = yes
VIAL_ENABLE = yes
VIAL_INSECURE = yes
TAP_DANCE_ENABLE = yes
COMBO_ENABLE = yes
QMK_SETTINGS = yes

# Board side (mini/): USB host queue, input -> matrix, TinyUSB callbacks.
SRC += hid_queue.c hid_scan.c hid_input.c hid_host.c

# Keymap side.
SRC += mouse.c ball.c mouse_keys.c modes.c locks.c pointer.c gesture.c wheel.c scroll_out.c
SRC += settings.c default_config.c macro.c raw_hid.c
SRC += cli.c
SRC += os_key_override.c jp_key_on_us_os_override.c us_key_on_jp_os_override.c
VPATH += keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial/cli
VPATH += keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial/key_override

GIT_DESCRIBE := $(shell git describe --tags --long --dirty="\*" 2>/dev/null)
CFLAGS += -DGIT_DESCRIBE=$(GIT_DESCRIBE)

# raw_hid.c answers both VIA and Vial; macro.c replaces the macro player.
$(BUILD_DIR)/obj_sekigon_keyboard_quantizer_mini_vial/quantum/via.o:: CFLAGS += -Draw_hid_receive=raw_hid_receive_vial
$(BUILD_DIR)/obj_sekigon_keyboard_quantizer_mini_vial/quantum/dynamic_keymap.o:: CFLAGS += -Ddynamic_keymap_macro_send=dynamic_keymap_macro_send_vial
SRC += tmk_core/protocol/bmp/via_qmk.c
