/* Host tests for the core0 input side: queue -> parser -> matrix and media
 * keys. */

#include <string.h>

#include "check.h"
#include "hid_input.h"
#include "hid_queue.h"

void hidq_barrier(void) {}
void hidq_wait_us(uint32_t us) {
    (void)us;
}

static uint16_t consumer_log[16];
static int      consumer_n;
static int      lost_n;
static int      mouse_reports;
static int16_t  last_x;

void kq_host_consumer(uint16_t usage) {
    if (consumer_n < 16) {
        consumer_log[consumer_n] = usage;
    }
    consumer_n++;
}
void kq_mouse_device_lost(void) {
    lost_n++;
}
void kq_mouse_report(const hid_mouse_t *m) {
    mouse_reports++;
    last_x = m->x;
}

static const uint8_t keyboard[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, //
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x19, 0x00, 0x29, 0xFF, //
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x00, 0xC0,
};

static const uint8_t mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0, 0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x19,
    0x00, 0x2A, 0x9C, 0x02, 0x15, 0x00, 0x26, 0x9C, 0x02, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
};

static uint8_t m[KQ_MATRIX_ROWS];

static bool cell(uint16_t usage) {
    return (m[usage / 8 + 1] >> (usage % 8)) & 1;
}

static void kb(uint8_t interface, uint8_t mods, uint8_t k1, uint8_t k2) {
    uint8_t r[8] = {mods, 0, k1, k2, 0, 0, 0, 0};
    hidq_push_report(interface, r, sizeof r);
}

static void test_matrix(void) {
    hidq_reset();
    memset(m, 0, sizeof m);
    hidq_push_mount(16, keyboard, sizeof keyboard);
    hidq_push_mount(32, keyboard, sizeof keyboard); /* a second keyboard */
    hidq_push_mount(17, mouse, sizeof mouse);
    CHECK(!hid_input_scan(m), "mounts changed the matrix");

    kb(16, 0x02, 0x04, 0); /* LShift + A on keyboard 1 */
    CHECK(hid_input_scan(m), "no change");
    CHECK(cell(0x04) && (m[0] & 0x02), "A / LShift missing");

    kb(32, 0, 0x05, 0); /* B on keyboard 2: keyboard 1's A stays */
    CHECK(hid_input_scan(m), "no change");
    CHECK(cell(0x04) && cell(0x05), "keyboards erased each other");
    CHECK_EQ(m[29], 0); /* modifiers are row 0, not their identity row */

    kb(32, 0, 0x05, 0xE9); /* E8..EF are ordinary cells (row 30) */
    CHECK(hid_input_scan(m) && cell(0xE9), "usage E9 missing");
    kb(32, 0, 0x05, 0);
    CHECK(hid_input_scan(m) && !cell(0xE9), "usage E9 stuck");
    /* F0..F7 would be row 31, the gesture cells (F8.. past the matrix): never
     * from a device. */
    kb(32, 0, 0x05, 0xF1);
    CHECK(!hid_input_scan(m) && m[31] == 0, "usage F1 reached row 31");

    /* Press and release of C in one go: two scans, one change each. */
    kb(16, 0x02, 0x04, 0x06);
    kb(16, 0x02, 0x04, 0);
    CHECK(hid_input_scan(m) && cell(0x06), "C press lost");
    CHECK(hid_input_scan(m) && !cell(0x06), "C release lost");
    CHECK(!hid_input_scan(m), "extra change");

    /* Mouse buttons land on the KC_MS_BTN cells; motion does not stop the
     * scan or touch the matrix. */
    uint8_t move[]  = {0x01, 0x00, 0x05};
    uint8_t click[] = {0x01, 0x01, 0x00};
    hidq_push_report(17, move, sizeof move);
    hidq_push_report(17, move, sizeof move);
    hidq_push_report(17, click, sizeof click);
    mouse_reports = 0;
    CHECK(hid_input_scan(m) && cell(0xD1), "click missing");
    CHECK_EQ(mouse_reports, 3);
    CHECK_EQ(last_x, 0);

    /* A Report ID the descriptor lacks is ignored, and counted. */
    uint32_t unknown   = hid_input_stats().unknown_reports;
    uint8_t  strange[] = {0x09, 0x01, 0x00};
    hidq_push_report(17, strange, sizeof strange);
    CHECK(!hid_input_scan(m), "unknown report ID changed the matrix");
    CHECK_EQ(hid_input_stats().unknown_reports, unknown + 1);

    /* Unplugging keyboard 2 releases its keys only; unplugging the mouse
     * releases the button and reports the loss. */
    hidq_push_unmount(32);
    CHECK(hid_input_scan(m) && !cell(0x05) && cell(0x04), "unmount of keyboard 2");
    lost_n = 0;
    hidq_push_unmount(17);
    CHECK(hid_input_scan(m) && !cell(0xD1), "button not released");
    CHECK_EQ(lost_n, 1);

    /* A report for an unmounted interface is ignored. */
    hidq_push_report(17, click, sizeof click);
    CHECK(!hid_input_scan(m), "report after unmount");
    CHECK(hid_input_stats().unknown_reports >= 1, "not counted");

    /* The last device (a keyboard) going away also drops the latches. */
    lost_n = 0;
    hidq_push_unmount(16);
    CHECK(hid_input_scan(m) && !cell(0x04), "keyboard 1 keys not released");
    CHECK_EQ(lost_n, 1);
}

/* A stream of motion reports cannot keep one scan going. */
static void test_scan_budget(void) {
    hidq_reset();
    memset(m, 0, sizeof m);
    hidq_push_mount(17, mouse, sizeof mouse);
    hid_input_scan(m);
    uint8_t move[] = {0x01, 0x00, 0x01};
    for (int i = 0; i < 100; i++) {
        hidq_push_report(17, move, sizeof move);
    }
    mouse_reports = 0;
    hid_input_scan(m);
    CHECK(mouse_reports > 0 && mouse_reports < 100, "one scan took %d reports", mouse_reports);
    while (mouse_reports < 100) {
        int before = mouse_reports;
        hid_input_scan(m);
        if (mouse_reports == before) {
            break;
        }
    }
    CHECK_EQ(mouse_reports, 100);
}

static void test_consumer(void) {
    hidq_reset();
    memset(m, 0, sizeof m);
    hidq_push_mount(17, mouse, sizeof mouse);
    hid_input_scan(m);
    consumer_n   = 0;
    uint8_t up[] = {0x02, 0xE9, 0x00}, none[] = {0x02, 0x00, 0x00};
    hidq_push_report(17, up, sizeof up);
    hidq_push_report(17, up, sizeof up); /* held: no second send */
    hidq_push_report(17, none, sizeof none);
    hid_input_scan(m);
    CHECK_EQ(consumer_n, 2);
    CHECK(consumer_log[0] == 0xE9 && consumer_log[1] == 0, "press/release");
    /* Unplug while held: released. */
    hidq_push_report(17, up, sizeof up);
    hidq_push_unmount(17);
    hid_input_scan(m);
    CHECK(consumer_n == 4 && consumer_log[3] == 0, "not released on unplug");
}

int main(void) {
    test_matrix();
    test_consumer();
    test_scan_budget();
    return check_done("input");
}
