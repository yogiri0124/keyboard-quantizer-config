/* Host tests for mini/hid_host.c (with the real hid_queue.c and hid_scan.c):
 * which interface gets the lock LEDs, what goes out and when, against
 * stand-ins for TinyUSB and the board. */

#include <string.h>

#include "check.h"
#include "qmk_stub.h"
#include "tusb.h"

void tuh_mount_cb(uint8_t dev_addr);
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len);
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance);
void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type,
                                    uint16_t len);
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len);
void housekeeping_task_kb(void);

/* ---- stand-ins ---- */

void hidq_barrier(void) {}
void hidq_wait_us(uint32_t us) {
    (void)us;
}

static uint8_t host_leds;
static bool    accept = true; /* what tuh_hid_set_report answers */

typedef struct {
    uint8_t  addr, instance, report_id, type;
    uint8_t  data[4];
    uint16_t len;
} sent_t;
static sent_t sent[8];
static int    n_sent;

bool tuh_hid_set_report(uint8_t dev_addr, uint8_t idx, uint8_t report_id, uint8_t report_type, void *report,
                        uint16_t len) {
    if (!accept) {
        return false;
    }
    if (n_sent < 8) {
        sent_t *s = &sent[n_sent++];
        *s        = (sent_t){dev_addr, idx, report_id, report_type, {0}, len};
        memcpy(s->data, report, len < 4 ? len : 4);
    }
    return true;
}
bool tuh_hid_receive_report(uint8_t dev_addr, uint8_t idx) {
    (void)dev_addr;
    (void)idx;
    return true;
}
uint8_t host_keyboard_leds(void) {
    return host_leds;
}
void gpio_set_pin_output_push_pull(pin_t pin) {
    (void)pin;
}
void gpio_write_pin_high(pin_t pin) {
    (void)pin;
}
void gpio_write_pin_low(pin_t pin) {
    (void)pin;
}
void     matrix_init_user(void) {}
uint16_t timer_read(void) {
    return 0;
}
uint16_t timer_elapsed(uint16_t last) {
    (void)last;
    return 0;
}

/* ---- devices ---- */

/* Boot keyboard with its 5 LEDs, no Report IDs. */
static const uint8_t keyboard[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0};
/* A keyboard in report 3 with its LED output. */
static const uint8_t keyboard_id3[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x03, 0x05, 0x07, 0x19, 0xE0,
                                       0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
                                       0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x91, 0x02, 0x95, 0x03,
                                       0x91, 0x01, 0xC0};
/* A 3-button mouse: no LEDs. */
static const uint8_t mouse[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19,
                                0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
                                0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15,
                                0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0};

static const uint8_t motion[] = {0x00, 0x01, 0x00};

static void set_host_leds(uint8_t leds) {
    host_leds = leds;
    housekeeping_task_kb(); /* core0 publishes it */
}

/* ---- tests ---- */

static void test_lock_leds(void) {
    set_host_leds(0x02); /* Caps Lock */
    tuh_hid_mount_cb(1, 0, mouse, sizeof mouse);
    tuh_mount_cb(1);
    tuh_hid_report_received_cb(1, 0, motion, sizeof motion);
    CHECK(n_sent == 0, "LEDs sent with only a mouse: %d", n_sent);

    /* A keyboard gets the current state as soon as it is mounted. */
    tuh_hid_mount_cb(2, 0, keyboard, sizeof keyboard);
    CHECK(n_sent == 1 && sent[0].addr == 2 && sent[0].instance == 0 && sent[0].report_id == 0 &&
              sent[0].type == HID_REPORT_TYPE_OUTPUT && sent[0].len == 1 && sent[0].data[0] == 0x02,
          "mount: %d sent", n_sent);

    /* While that transfer runs nothing else goes out, and a completion for
     * another device does not end it. */
    set_host_leds(0x03);
    tuh_hid_report_received_cb(1, 0, motion, sizeof motion);
    tuh_hid_set_report_complete_cb(1, 0, 0, HID_REPORT_TYPE_OUTPUT, 1);
    CHECK_EQ(n_sent, 1);
    /* Its own completion lets the new state out. */
    tuh_hid_set_report_complete_cb(2, 0, 0, HID_REPORT_TYPE_OUTPUT, 1);
    CHECK(n_sent == 2 && sent[1].data[0] == 0x03, "after completion: %d sent", n_sent);
    tuh_hid_set_report_complete_cb(2, 0, 0, HID_REPORT_TYPE_OUTPUT, 1);
    tuh_hid_report_received_cb(1, 0, motion, sizeof motion);
    CHECK(n_sent == 2, "unchanged state sent again");

    /* A refused transfer is tried again with the next callback. */
    accept = false;
    set_host_leds(0x01);
    tuh_hid_report_received_cb(1, 0, motion, sizeof motion);
    accept = true;
    CHECK_EQ(n_sent, 2);
    tuh_hid_report_received_cb(1, 0, motion, sizeof motion);
    CHECK(n_sent == 3 && sent[2].data[0] == 0x01, "retry: %d sent", n_sent);

    /* A newer keyboard takes over, with its Report ID in front, even while
     * the old one's transfer is still running; that one's completion then
     * does not free the buffer the new transfer uses. */
    tuh_hid_mount_cb(3, 1, keyboard_id3, sizeof keyboard_id3);
    CHECK(n_sent == 4 && sent[3].addr == 3 && sent[3].instance == 1 && sent[3].report_id == 3 && sent[3].len == 2 &&
              sent[3].data[0] == 3 && sent[3].data[1] == 0x01,
          "report ID keyboard: %d sent", n_sent);
    set_host_leds(0x04);
    tuh_hid_set_report_complete_cb(2, 0, 0, HID_REPORT_TYPE_OUTPUT, 1);
    CHECK_EQ(n_sent, 4);
    tuh_hid_set_report_complete_cb(3, 1, 3, HID_REPORT_TYPE_OUTPUT, 2);
    CHECK(n_sent == 5 && sent[4].addr == 3 && sent[4].data[1] == 0x04, "after takeover: %d sent", n_sent);

    /* Unplugged mid-transfer: nothing waits for it, nothing goes out. */
    tuh_hid_umount_cb(3, 1);
    set_host_leds(0x00);
    tuh_hid_report_received_cb(1, 0, motion, sizeof motion);
    CHECK_EQ(n_sent, 5);
    /* Plugged in again: the state goes out at once. */
    tuh_hid_mount_cb(3, 1, keyboard_id3, sizeof keyboard_id3);
    CHECK(n_sent == 6 && sent[5].data[1] == 0x00, "replug: %d sent", n_sent);
}

int main(void) {
    test_lock_leds();
    return check_done("host callbacks / LEDs");
}
