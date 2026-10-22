/* Just enough of QMK for host tests of firmware files that include it
 * (built with -DQMK_KEYBOARD_H='"qmk_stub.h"' and -I tools/test/stubs).
 * Values are QMK's own; the functions are defined by each test, which
 * records what was called. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "action.h"
#include "keycodes.h"
#include "report.h"

#define TAP_CODE_DELAY 0
#define DYNAMIC_KEYMAP_MACRO_COUNT 16
#define MATRIX_ROWS 32
#define MATRIX_COLS 8

void wait_ms(uint32_t ms);

/* quantum/action_util.h */
typedef enum { ONESHOT_PRESSED = 1, ONESHOT_OTHER_KEY_PRESSED = 2 } oneshot_fullfillment_t;
void    clear_oneshot_layer_state(oneshot_fullfillment_t state);
void    clear_oneshot_mods(void);
uint8_t get_mods(void);
uint8_t get_weak_mods(void);
uint8_t get_oneshot_mods(void);

/* quantum/action_layer.h, keymap_common.h */
typedef uint32_t     layer_state_t;
extern layer_state_t layer_state;
extern layer_state_t default_layer_state;
uint8_t              get_highest_layer(layer_state_t state);
uint8_t              layer_switch_get_layer(keypos_t key);
uint16_t             keymap_key_to_keycode(uint8_t layer, keypos_t key);

/* quantum/action_util.h, keycode_config.h */
#define MOD_BIT(code) (1 << ((code)&0x07))
void     register_mods(uint8_t mods);
void     unregister_mods(uint8_t mods);
uint16_t keycode_config(uint16_t keycode);
typedef struct {
    bool swap_lalt_lgui;
} keymap_config_t;
extern keymap_config_t keymap_config;

/* quantum/send_string.h */
void send_string(const char *string);

/* quantum/action.h, timer.h, host.h */
void     action_exec(keyevent_t event);
uint16_t timer_read(void);
uint16_t timer_elapsed(uint16_t last);
void     host_mouse_send(report_mouse_t *report);

/* platforms/gpio.h, quantum/led.h, quantum/matrix.h */
typedef uint32_t pin_t;
void             gpio_set_pin_output_push_pull(pin_t pin);
void             gpio_write_pin_high(pin_t pin);
void             gpio_write_pin_low(pin_t pin);
uint8_t          host_keyboard_leds(void);
void             matrix_init_user(void);
