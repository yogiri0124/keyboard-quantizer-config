/* Host-test stand-in for quantum/action.h and quantum/keyboard.h. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t col;
    uint8_t row;
} keypos_t;

typedef enum { TICK_EVENT = 0, KEY_EVENT = 1, COMBO_EVENT = 4 } keyevent_type_t;

typedef struct {
    keypos_t        key;
    uint16_t        time;
    keyevent_type_t type;
    bool            pressed;
} keyevent_t;

static inline bool IS_KEYEVENT(const keyevent_t event) {
    return event.type == KEY_EVENT;
}
static inline bool IS_COMBOEVENT(const keyevent_t event) {
    return event.type == COMBO_EVENT;
}

typedef struct {
    bool    interrupted : 1;
    uint8_t reserved : 3;
    uint8_t count : 4;
} tap_t;

typedef struct {
    keyevent_t event;
    tap_t      tap;
    uint16_t   keycode;
} keyrecord_t;
