/* Host tests for the core1 -> core0 USB event queue (single-threaded: the
 * producer and consumer steps are interleaved by hand). */

#include <string.h>

#include "check.h"
#include "hid_queue.h"

void hidq_barrier(void) {}
void hidq_wait_us(uint32_t us) {
    (void)us;
}

static void push_tagged(uint8_t interface, uint16_t tag, uint16_t len) {
    uint8_t buf[HIDQ_REPORT_MAX] = {0};
    buf[0]                       = (uint8_t)tag;
    buf[1]                       = (uint8_t)(tag >> 8);
    hidq_push_report(interface, buf, len);
}

static uint16_t tag_of(const hidq_event_t *ev) {
    return (uint16_t)(ev->data[0] | (ev->data[1] << 8));
}

static void test_order(void) {
    hidq_reset();
    uint8_t desc[200];
    memset(desc, 0xAB, sizeof desc);
    hidq_push_mount(17, desc, sizeof desc);
    push_tagged(17, 1, 8);
    push_tagged(16, 2, 3);
    hidq_push_unmount(17);
    hidq_event_t ev;
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_MOUNT && ev.interface == 17 && ev.len == 200 && ev.data[199] == 0xAB, "mount");
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_REPORT && ev.interface == 17 && tag_of(&ev) == 1 && ev.len == 8, "report 1");
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_REPORT && ev.interface == 16 && tag_of(&ev) == 2 && ev.len == 3, "report 2");
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_UNMOUNT && ev.interface == 17, "unmount");
    CHECK(!hidq_pop(&ev), "queue not empty");
}

/* Many passes around the ring with odd sizes: wraparound padding. */
static void test_wrap(void) {
    hidq_reset();
    hidq_event_t ev;
    uint16_t     next_pop = 0;
    for (uint16_t i = 0; i < 5000; i++) {
        push_tagged((uint8_t)(i & 7), i, (uint16_t)(2 + (i % 61)));
        if (i % 3 == 2) {
            while (hidq_pop(&ev)) {
                CHECK(tag_of(&ev) == next_pop && ev.len == 2 + (next_pop % 61), "wrap order at %u", next_pop);
                next_pop++;
            }
        }
    }
    while (hidq_pop(&ev)) {
        CHECK(tag_of(&ev) == next_pop, "tail order at %u", next_pop);
        next_pop++;
    }
    CHECK_EQ(next_pop, 5000);
    CHECK_EQ(hidq_stats().slot_reports, 0);
}

/* Overflow: the ring fills, the newest report per interface waits in a slot
 * and comes out after the ring records that were older, and before the ones
 * pushed once the ring had room again. */
static void test_overflow(void) {
    hidq_reset();
    hidq_event_t ev;
    uint16_t     tag = 0;
    while (hidq_stats().slot_reports == 0) {
        push_tagged((uint8_t)(tag & 1), tag, 64);
        tag++;
    }
    uint16_t first_slot = (uint16_t)(tag - 1);
    /* More overflow: only the newest per interface is kept. */
    for (int i = 0; i < 10; i++) {
        push_tagged((uint8_t)(tag & 1), tag, 64);
        tag++;
    }
    uint16_t newest0 = (uint16_t)((tag - 1) & 1 ? tag - 2 : tag - 1);
    uint16_t newest1 = (uint16_t)((tag - 1) & 1 ? tag - 1 : tag - 2);
    /* A mount during overflow uses the reserve and keeps its place. */
    uint8_t desc[4] = {1, 2, 3, 4};
    hidq_push_mount(5, desc, sizeof desc);

    uint16_t expect = 0;
    while (expect < first_slot) {
        CHECK(hidq_pop(&ev) && ev.type == HIDQ_REPORT && tag_of(&ev) == expect, "ring report %u", expect);
        expect++;
    }
    /* The two slots, oldest stamp first: whichever interface wrote last
     * holds the larger stamp. */
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_REPORT, "slot A");
    uint16_t a = tag_of(&ev);
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_REPORT, "slot B");
    uint16_t b = tag_of(&ev);
    CHECK((a == newest0 && b == newest1) || (a == newest1 && b == newest0), "slots %u %u", a, b);
    CHECK(a < b, "slots out of stamp order");
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_MOUNT && ev.interface == 5, "mount after the slots");
    CHECK(!hidq_pop(&ev), "left over");

    /* Room again: reports go to the ring. */
    uint32_t slot_before = hidq_stats().slot_reports;
    push_tagged(0, 900, 8);
    CHECK(hidq_pop(&ev) && tag_of(&ev) == 900, "back to the ring");
    CHECK_EQ(hidq_stats().slot_reports, slot_before);
}

/* Slot written when the ring was partly drained: it is still handed out
 * before records pushed after it. */
static void test_slot_position(void) {
    hidq_reset();
    hidq_event_t ev;
    uint16_t     tag = 0;
    while (hidq_stats().slot_reports == 0) {
        push_tagged(0, tag, 64);
        tag++;
    }
    uint16_t slot_tag = (uint16_t)(tag - 1);
    /* Drain just below half: still in overflow, so the next push replaces
     * the slot (newest wins, same position). */
    for (uint16_t i = 0; i < 10; i++) {
        CHECK(hidq_pop(&ev) && tag_of(&ev) == i, "drain %u", i);
    }
    push_tagged(0, 1000, 64);
    slot_tag = 1000;
    /* Drain the rest of the ring: the slot comes right after. */
    for (uint16_t i = 10; i < tag - 1; i++) {
        CHECK(hidq_pop(&ev) && tag_of(&ev) == i, "ring %u", i);
    }
    CHECK(hidq_pop(&ev) && tag_of(&ev) == slot_tag, "slot after ring");
    push_tagged(0, 2000, 8);
    CHECK(hidq_pop(&ev) && tag_of(&ev) == 2000, "new ring record");
    CHECK(!hidq_pop(&ev), "left over");
}

/* An unmount drops the interface's waiting slot report. */
static void test_unmount_drops_slot(void) {
    hidq_reset();
    hidq_event_t ev;
    uint16_t     tag = 0;
    while (hidq_stats().slot_reports == 0) {
        push_tagged(3, tag, 64);
        tag++;
    }
    hidq_push_unmount(3);
    for (uint16_t i = 0; i < tag - 1; i++) {
        CHECK(hidq_pop(&ev) && ev.type == HIDQ_REPORT && tag_of(&ev) == i, "ring %u", i);
    }
    CHECK(hidq_pop(&ev) && ev.type == HIDQ_UNMOUNT && ev.interface == 3, "unmount, no slot report");
    CHECK(!hidq_pop(&ev), "left over");
}

int main(void) {
    test_order();
    test_wrap();
    test_overflow();
    test_slot_position();
    test_unmount_drops_slot();
    return check_done("queue");
}
