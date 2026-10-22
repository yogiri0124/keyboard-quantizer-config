/* Host tests for ball.c and wheel.c (with the real gesture, pointer,
 * scroll_out and modes), against a small simulated QMK: a keymap, layers,
 * and an action_exec that resolves keys and records taps. */

#include <string.h>

#include "check.h"
#include "qmk_stub.h"
#include "dynamic_keymap.h"
#include "usb_main.h"

#include "ball.h"
#include "gesture.h"
#include "hid_input.h"
#include "kq_keycodes.h"
#include "modes.h"
#include "mouse.h"
#include "scroll_out.h"
#include "settings.h"
#include "wheel.h"

/* ---- simulated QMK ---- */

static uint16_t   km[8][MATRIX_ROWS][MATRIX_COLS];
layer_state_t     layer_state;
layer_state_t     default_layer_state = 1;
static uint32_t   now                 = 1000;
static uint8_t    mods;
static uint8_t    room = 7;
static bool       tap_hold_pending;
uint8_t           mk_wheel_delay    = 10; /* 100 ms */
uint8_t           mk_wheel_interval = 50;
usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];

static struct {
    keypos_t key;
    uint16_t kc;
} taps[64];
static int n_taps;

uint8_t get_highest_layer(layer_state_t s) {
    for (int l = 31; l > 0; l--) {
        if ((s >> l) & 1) {
            return (uint8_t)l;
        }
    }
    return 0;
}
uint16_t keymap_key_to_keycode(uint8_t layer, keypos_t key) {
    if (key.row == 31 && layer == 0) {
        layer = 1; /* as keymap.c */
    }
    return km[layer][key.row][key.col];
}
uint8_t layer_switch_get_layer(keypos_t key) {
    layer_state_t s = layer_state | default_layer_state;
    for (int l = 7; l >= 0; l--) {
        if (((s >> l) & 1) && keymap_key_to_keycode((uint8_t)l, key) != KC_TRNS) {
            return (uint8_t)l;
        }
    }
    return 0;
}
uint16_t dynamic_keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t col) {
    return km[layer][row][col];
}
/* QMK in short: resolve the key, record the press, let process_record (the
 * wheel's part of it) see it, and run CLR if it gets that far. */
void action_exec(keyevent_t event) {
    uint16_t    kc = keymap_key_to_keycode(layer_switch_get_layer(event.key), event.key);
    keyrecord_t r  = {.event = event};
    if (tap_hold_pending && room > 0) {
        room--; /* while a tap-hold key is undecided every event queues */
    }
    if (event.pressed && n_taps < 64) {
        taps[n_taps].key = event.key;
        taps[n_taps].kc  = kc;
        n_taps++;
    }
    if (!wheel_process_record(kc, &r) || !event.pressed) {
        return;
    }
    if (kc == U_CLR) {
        mouse_reset_latches();
    }
}
uint16_t timer_read(void) {
    return (uint16_t)now;
}
uint16_t timer_elapsed(uint16_t last) {
    return (uint16_t)((uint16_t)now - last);
}
uint32_t timer_read32(void) {
    return now;
}
uint32_t timer_elapsed32(uint32_t last) {
    return now - last;
}
uint8_t get_mods(void) {
    return mods;
}
uint8_t get_weak_mods(void) {
    return 0;
}
uint8_t get_oneshot_mods(void) {
    return 0;
}
void wait_ms(uint32_t ms) {
    now += ms;
}
bool usb_endpoint_in_is_inactive(usb_endpoint_in_t *e) {
    (void)e;
    return true;
}
uint8_t usb_mouse_wheel_multiplier(bool vertical) {
    (void)vertical;
    return 120;
}

/* ---- stand-ins for mouse.c and settings.c ---- */

static int32_t motion_x, motion_y, direct_v, direct_h;
static int     inject_depth, direct_sends;

void mouse_request_send(void) {}
void mouse_add_motion(int32_t x, int32_t y) {
    motion_x += x;
    motion_y += y;
}
void mouse_send_direct(const report_mouse_t *m) {
    direct_v += m->v;
    direct_h += m->h;
    direct_sends++;
}
void mouse_tap_cell(keypos_t pos, uint16_t hold_ms) {
    inject_depth++;
    action_exec((keyevent_t){.type = KEY_EVENT, .key = pos, .pressed = true, .time = timer_read() | 1});
    wait_ms(hold_ms);
    action_exec((keyevent_t){.type = KEY_EVENT, .key = pos, .pressed = false, .time = timer_read() | 1});
    inject_depth--;
}
bool mouse_injecting(void) {
    return inject_depth != 0;
}
bool mouse_can_tap(void) {
    return room >= 2;
}
void mouse_reset_latches(void) {
    modes_reset();
    wheel_reset();
    scroll_out_reset();
    ball_reset();
}
uint8_t settings_get(setting_t s) {
    return s == SETTING_SCROLL_DIV ? 32 : s == SETTING_GESTURE_STEP ? 50 : 16;
}

/* ---- helpers ---- */

static void fresh(void) {
    memset(km, 0, sizeof km);
    for (uint16_t u = 0; u < 0xF0; u++) {
        km[0][u / 8 + 1][u % 8] = u; /* the identity layer */
    }
    for (int l = 1; l < 8; l++) {
        for (int r = 0; r < MATRIX_ROWS; r++) {
            for (int c = 0; c < MATRIX_COLS; c++) {
                km[l][r][c] = KC_TRNS;
            }
        }
    }
    layer_state      = 0;
    mods             = 0;
    room             = 7;
    n_taps           = 0;
    tap_hold_pending = false;
    motion_x = motion_y = direct_v = direct_h = 0;
    direct_sends                              = 0;
    mouse_reset_latches();
    ball_init();
}

/* Room is refilled each scan, as QMK drains its queue. */
static void scan(void) {
    room = 7;
    kq_mouse_scan_end();
    now++;
}

static void take_all(int32_t *v, int32_t *h) {
    int8_t tv, th;
    *v = *h = 0;
    for (int i = 0; i < 1000 && scroll_out_pending(); i++) {
        scroll_out_take(&tv, &th);
        *v += tv;
        *h += th;
    }
}

static void move(int16_t x, int16_t y) {
    hid_mouse_t m = {0};
    m.x           = x;
    m.y           = y;
    kq_mouse_report(&m);
}

static void wheel(int16_t w, int16_t pan) {
    hid_mouse_t m = {0};
    m.wheel       = w;
    m.pan         = pan;
    kq_mouse_report(&m);
}

/* ---- wheel ---- */

static void test_wheel_cells(void) {
    int32_t v, h;
    fresh();
    wheel(2, 0);
    scan();
    CHECK_EQ(n_taps, 2);
    take_all(&v, &h);
    CHECK(v == 240 && h == 0, "2 detents up: v %ld h %ld", (long)v, (long)h);

    fresh();
    km[0][28][1] = KC_MS_WH_DOWN; /* the up cell inverted */
    wheel(1, 0);
    scan();
    take_all(&v, &h);
    CHECK_EQ(v, -120);

    fresh();
    km[0][28][1] = KC_MS_WH_RIGHT; /* the up cell scrolls right */
    wheel(1, 0);
    scan();
    take_all(&v, &h);
    CHECK(v == 0 && h == 120, "other axis: v %ld h %ld", (long)v, (long)h);

    fresh();
    wheel(0, 1); /* HUGE PLUS: positive pan = tilted left */
    scan();
    CHECK(n_taps == 1 && taps[0].key.row == 28 && taps[0].key.col == 3, "pan +1 tapped %u,%u", taps[0].key.row,
          taps[0].key.col);
    take_all(&v, &h);
    CHECK_EQ(h, 120); /* passed through as the device sent it */

    fresh();
    km[0][28][2] = KC_A; /* down cell holds a letter: one tap per detent */
    wheel(-3, 0);
    scan();
    CHECK(n_taps == 3 && taps[0].kc == KC_A && taps[2].kc == KC_A, "letter taps %d", n_taps);
    CHECK(!scroll_out_pending(), "a letter scrolled");
}

static void test_wheel_limits(void) {
    fresh();
    wheel(20, 0);
    scan();
    CHECK_EQ(n_taps, 8); /* per scan */
    scan();
    scan();
    CHECK_EQ(n_taps, 20);

    fresh();
    wheel(3, 0);
    room = 0; /* QMK's queue is full */
    kq_mouse_scan_end();
    CHECK_EQ(n_taps, 0);
    scan();
    CHECK_EQ(n_taps, 3);

    fresh();
    tap_hold_pending = true; /* each tap queues two events: 3 fit in 7 */
    wheel(5, 0);
    kq_mouse_scan_end();
    CHECK_EQ(n_taps, 3);
    tap_hold_pending = false;
    scan();
    CHECK_EQ(n_taps, 5);

    fresh();
    km[0][28][1] = U_CLR; /* CLR on the up cell: no detent may flip */
    wheel(3, 0);
    scan();
    scan();
    CHECK(n_taps == 1 && taps[0].kc == U_CLR, "CLR cell taps %d", n_taps);
}

static void test_wheel_mods_and_keys(void) {
    int32_t v, h;
    fresh();
    mods = 0x01; /* Ctrl held: sent at once, on its own */
    wheel(1, 0);
    scan();
    CHECK(direct_v == 120 && direct_sends == 1, "urgent v %ld sends %d", (long)direct_v, direct_sends);
    take_all(&v, &h);
    CHECK_EQ(v, 0);

    /* A key mapped to WH_UP elsewhere: one detent, then QMK's repeat. */
    fresh();
    keyrecord_t r = {.event = {.key = {.row = 1, .col = 4}, .type = KEY_EVENT, .pressed = true}};
    CHECK(!wheel_process_record(KC_MS_WH_UP, &r), "WH key reached QMK");
    take_all(&v, &h);
    CHECK_EQ(v, 120);
    now += 99;
    wheel_task();
    CHECK(!scroll_out_pending(), "repeat before the delay");
    now += 2;
    wheel_task();
    take_all(&v, &h);
    CHECK_EQ(v, 120);
    r.event.pressed = false;
    wheel_process_record(KC_MS_WH_UP, &r);
    now += 200;
    wheel_task();
    CHECK(!scroll_out_pending(), "repeat after release");
}

/* ---- ball ---- */

static void test_ball_routes(void) {
    int32_t v, h;
    fresh();
    move(5, -3);
    CHECK(motion_x == 5 && motion_y == -3, "cursor %ld %ld", (long)motion_x, (long)motion_y);

    fresh();
    modes_hold_key(KQ_MODE_SCROLL, true, false);
    move(0, -64); /* ball up two detents' worth */
    take_all(&v, &h);
    CHECK(v == 240 && motion_y == 0, "scroll v %ld", (long)v);
    modes_hold_key(KQ_MODE_SCROLL, false, false);
}

static void test_ball_gestures(void) {
    fresh();
    km[1][31][GESTURE_RIGHT]    = KC_A;
    km[1][31][GESTURE_UP_RIGHT] = KC_B;
    modes_hold_key(KQ_MODE_GESTURE, true, false); /* on layer 0: layer 1's cells */
    move(60, 0);
    CHECK(n_taps == 1 && taps[0].kc == KC_A && taps[0].key.row == 31, "right gesture: %d taps", n_taps);
    CHECK(motion_x == 0, "gesture moved the cursor");
    now += 200;
    scan(); /* the stroke ends */
    move(40, -40);
    CHECK(n_taps == 2 && taps[1].kc == KC_B, "up-right gesture gave 0x%04X", taps[1].kc);

    /* Without the diagonal mapped, the same move is right or up. */
    km[1][31][GESTURE_UP_RIGHT] = KC_TRNS;
    km[1][31][GESTURE_UP]       = KC_TRNS;
    now                         = now + 500;
    scan();
    move(45, -40);
    move(10, 0);
    CHECK(n_taps == 3 && taps[2].kc == KC_A, "fallback gave %d taps, 0x%04X", n_taps, taps[2].kc);
    modes_hold_key(KQ_MODE_GESTURE, false, false);

    /* After the ball rested for over a minute (a 16-bit clock wraps), a
     * keymap change made meanwhile is read, not an old cached view. */
    fresh();
    km[1][31][GESTURE_RIGHT]    = KC_A;
    km[1][31][GESTURE_UP_RIGHT] = KC_B;
    modes_hold_key(KQ_MODE_GESTURE, true, false);
    move(40, -40);
    CHECK(n_taps == 1 && taps[0].kc == KC_B, "diagonal before the rest: %d taps", n_taps);
    now += 200;
    scan();
    now += 65536 - 200;
    km[1][31][GESTURE_UP_RIGHT] = KC_TRNS; /* unmapped in Vial while resting */
    move(45, -40);
    move(10, 0);
    CHECK(n_taps == 2 && taps[1].kc == KC_A, "after the rest: %d taps, 0x%04X", n_taps, taps[1].kc);
    modes_hold_key(KQ_MODE_GESTURE, false, false);

    /* No room in QMK's queue: the tap waits, then goes. */
    fresh();
    km[1][31][GESTURE_LEFT] = KC_A;
    modes_hold_key(KQ_MODE_GESTURE, true, false);
    room = 0;
    move(-60, 0);
    CHECK_EQ(n_taps, 0);
    scan();
    CHECK_EQ(n_taps, 1);

    /* CLR in a gesture cell: taps that were waiting are dropped with it. */
    fresh();
    km[1][31][GESTURE_LEFT] = U_CLR;
    modes_hold_key(KQ_MODE_GESTURE, true, false);
    room = 0;
    move(-60, 0);
    now += 50;
    move(-60, 0);
    scan();
    CHECK(n_taps == 1 && !modes_active(KQ_MODE_GESTURE), "CLR gesture: %d taps", n_taps);
}

static void test_gestures_by_layout(void) {
    fresh();
    layer_state             = 1u << 2; /* layer 2 maps a gesture cell, no ball cells */
    km[2][31][GESTURE_DOWN] = KC_A;
    for (int u = KC_MS_UP; u <= KC_MS_RIGHT; u++) {
        km[2][u / 8 + 1][u % 8] = KC_NO;
    }
    move(0, 60);
    CHECK(n_taps == 1 && taps[0].kc == KC_A, "layout gesture: %d taps", n_taps);

    fresh();
    layer_state             = 1u << 2;
    km[2][31][GESTURE_DOWN] = KC_A;
    km[2][26][5]            = KC_B; /* a ball-direction cell mapped: cursor */
    move(0, 60);
    CHECK(n_taps == 0 && motion_y == 60, "ball cell mapped: %d taps, y %ld", n_taps, (long)motion_y);
}

int main(void) {
    test_wheel_cells();
    test_wheel_limits();
    test_wheel_mods_and_keys();
    test_ball_routes();
    test_ball_gestures();
    test_gestures_by_layout();
    return check_done("ball / wheel");
}
