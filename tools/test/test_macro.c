/* Host tests for the macro player (keymaps/vial/macro.c), against a fake
 * macro buffer, recording what it plays. */

#include <stdio.h>
#include <string.h>

#include "check.h"
#include "dynamic_keymap.h"
#include "send_string.h"
#include "vial.h"

void dynamic_keymap_macro_send(uint8_t id);

static uint8_t  buffer[64];
static uint16_t buffer_size = sizeof(buffer);
static char     played[256];

static void log_event(const char *fmt, unsigned a, unsigned b) {
    char item[32];
    snprintf(item, sizeof item, fmt, a, b);
    strncat(played, item, sizeof(played) - strlen(played) - 1);
}

uint16_t dynamic_keymap_macro_get_buffer_size(void) {
    return buffer_size;
}
void dynamic_keymap_macro_get_buffer(uint16_t offset, uint16_t size, uint8_t *data) {
    for (uint16_t i = 0; i < size; i++) {
        data[i] = offset + i < sizeof(buffer) ? buffer[offset + i] : 0;
    }
}
void send_string(const char *s) {
    log_event("ss(%u,%u)", (uint8_t)s[1], (uint8_t)s[2]);
    CHECK(s[3] == 0, "send_string got a longer string");
}
void send_string_with_delay(const char *s, uint8_t interval) {
    (void)interval;
    log_event("c(%c)", (unsigned)s[0], 0);
    CHECK(s[1] == 0, "text sent with more than one char");
}
void vial_keycode_tap(uint16_t kc) {
    log_event("tap(%04X)", kc, 0);
}
void vial_keycode_down(uint16_t kc) {
    log_event("down(%04X)", kc, 0);
}
void vial_keycode_up(uint16_t kc) {
    log_event("up(%04X)", kc, 0);
}
static unsigned waited;
void            wait_ms(uint32_t ms) {
    waited += ms;
}

static void load(const uint8_t *bytes, size_t n) {
    memset(buffer, 0, sizeof buffer);
    memcpy(buffer, bytes, n);
    buffer_size = sizeof(buffer);
    played[0]   = 0;
    waited      = 0;
}

int main(void) {
    /* Macro 0: "a", tap of 0x04, Vial tap of LCTL(C) = 0x0106, Vial tap of
     * 0x0100 (sent as 0xFF01), Vial delay 300 ms, VIA delay "1500|", then
     * "b". Macro 1: down / up of 0x04. */
    static const uint8_t two[] = {
        'a', 1, 1,   0x04, 1,   5,   0x06, 0x01, 1, 5, 0x01, 0xFF, 1, 4, 300 % 255 + 1, 300 / 255 + 1,
        1,   4, '1', '5',  '0', '0', '|',  'b',  0, 1, 2,    0x04, 1, 3, 0x04,          0,
    };
    load(two, sizeof two);
    dynamic_keymap_macro_send(0);
    CHECK(strcmp(played, "c(a)ss(1,4)tap(0106)tap(0100)c(b)") == 0, "macro 0 played %s", played);
    CHECK_EQ(waited, 1800);
    played[0] = 0;
    dynamic_keymap_macro_send(1);
    CHECK(strcmp(played, "ss(2,4)ss(3,4)") == 0, "macro 1 played %s", played);

    /* Five digits without '|' are not VIA's: the Vial bytes follow. */
    static const uint8_t vial_digits[] = {1, 4, '1', '2', 0};
    load(vial_digits, sizeof vial_digits);
    dynamic_keymap_macro_send(0);
    CHECK_EQ(waited, ('1' - 1) + ('2' - 1) * 255);

    /* A macro cut short inside an item ends there, without playing the next
     * macro's bytes. */
    static const uint8_t cut[] = {1, 5, 0x06, 0, 'x', 0};
    load(cut, sizeof cut);
    dynamic_keymap_macro_send(0);
    CHECK(played[0] == 0, "cut macro played %s", played);

    /* Half-written buffer (last byte not 0): nothing plays. */
    load(two, sizeof two);
    buffer[sizeof(buffer) - 1] = 'z';
    dynamic_keymap_macro_send(0);
    CHECK(played[0] == 0, "half-written buffer played %s", played);

    /* Macro past the last one: nothing. */
    load(two, sizeof two);
    dynamic_keymap_macro_send(15);
    CHECK(played[0] == 0, "empty macro played %s", played);
    dynamic_keymap_macro_send(16);
    CHECK(played[0] == 0, "out of range played %s", played);

    return check_done("macro");
}
