// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "print.h"

#include <string.h>

#include "tusb.h"
#include "pio_usb_ll.h"
#include "report_descriptor_parser.h"
#include "report_parser.h"
#ifdef MOUSEKEY_ENABLE
#    include "mousekey.h"
#endif

matrix_row_t*           matrix_dest;
bool                    mouse_send_flag = false;
static volatile uint8_t kbd_addr;
static volatile uint8_t kbd_instance;
static volatile int32_t led_count = -1;
#define HID_RX_QUEUE 32
#define HID_UNMOUNT_QUEUE 8
#define HID_MOUNT_QUEUE 8
#define HID_DESC_MAX 256
_Static_assert(MATRIX_ROWS == 32 && MATRIX_COLS == 8, "identity matrix is 32x8");
_Static_assert(HID_DESC_MAX == CFG_TUH_ENUMERATION_BUFSIZE, "mount descriptor buffer must match TinyUSB enum buf");
_Static_assert(HID_DEVICE_COUNT == CFG_TUH_HID, "HID device table must match TinyUSB HID instances");
static uint8_t          hid_rx_buf[HID_RX_QUEUE][64];
static uint8_t          hid_rx_len[HID_RX_QUEUE];
static uint8_t          hid_rx_inst[HID_RX_QUEUE];
static uint8_t          hid_rx_epoch[HID_RX_QUEUE];
static volatile uint8_t hid_rx_head;
static volatile uint8_t hid_rx_tail;
/* Overflow slot, published with a seqlock: core1 overwrites it with the newest
 * report and never blocks, core0 throws away a copy that a write overlapped and
 * picks the newer one up on the next scan. Refusing to overwrite an occupied
 * slot kept the oldest overflow report instead of the newest, which is exactly
 * the wheel packet this slot exists to preserve. */
static uint8_t           hid_rx_overflow_buf[64];
static uint8_t           hid_rx_overflow_len;
static uint8_t           hid_rx_overflow_inst;
static uint8_t           hid_rx_overflow_epoch;
static volatile uint16_t hid_rx_overflow_seq;   /* odd while core1 is writing */
static uint16_t          hid_rx_overflow_taken; /* last even seq core0 consumed */
static uint8_t          hid_unmount_ids[HID_UNMOUNT_QUEUE];
static uint8_t          hid_unmount_epoch[HID_UNMOUNT_QUEUE];
static volatile uint8_t hid_unmount_head;
static volatile uint8_t hid_unmount_tail;
static uint8_t          hid_mount_ids[HID_MOUNT_QUEUE];
static uint16_t         hid_mount_len[HID_MOUNT_QUEUE];
static uint8_t          hid_mount_epoch[HID_MOUNT_QUEUE];
static uint8_t          hid_mount_desc[HID_MOUNT_QUEUE][HID_DESC_MAX];
static volatile uint8_t hid_mount_head;
static volatile uint8_t hid_mount_tail;
static volatile uint8_t hid_if_epoch[256];
static bool             hid_if_mouse[256];
static bool             hid_if_keyboard[256];
static bool             hid_keyboard_need_release;
static uint8_t          hid_mounted_count;
static volatile bool    hid_disconnect_flag;
static volatile bool    hid_mouse_lost_flag;
static bool             hid_mouse_need_reset;
static volatile bool    hid_unmount_overflow;
static volatile uint8_t hid_unmount_overflow_id;
static volatile uint8_t hid_unmount_overflow_epoch;
static volatile bool    hid_mount_overflow;
static volatile uint8_t hid_mount_overflow_id;
static volatile uint8_t hid_mount_overflow_epoch;
static volatile uint16_t hid_mount_overflow_len;
static uint8_t          hid_mount_overflow_desc[HID_DESC_MAX];
#define LED_BLINK_TIME_MS 50
#define KQ_PIN_LED 7
/* These waits run on core1, inside the TinyUSB host callbacks. Spinning there
 * stalls tuh_task(), so the hot report path waits only briefly and then falls
 * back to the overflow slot (which never loses the newest report). Mount and
 * unmount are rare, already slow, and must not be dropped, so they may wait. */
#define HID_RX_WAIT_US 200
#define HID_EVENT_WAIT_US 2000

extern void busy_wait_us(uint64_t delay_us);
static bool send_led_report(uint8_t* leds);

__attribute__((weak)) void keyboard_report_post_hook(void) {}
__attribute__((weak)) void mouse_on_host_disconnect(void) {}
__attribute__((weak)) void mouse_scan_end(void) {}
__attribute__((weak)) void mouse_after_send(void) {}
__attribute__((weak)) void mouse_wheel_flush(report_mouse_t* mouse) {
    (void)mouse;
}
__attribute__((weak)) void mouse_merge_buttons(report_mouse_t* mouse) {
#ifdef MOUSEKEY_ENABLE
    if (mouse != NULL) {
        mouse->buttons |= mousekey_get_report().buttons;
    }
#else
    (void)mouse;
#endif
}
__attribute__((weak)) void hid_note_consumer(uint16_t report) {
    (void)report;
}
__attribute__((weak)) void hid_note_system(uint16_t report) {
    (void)report;
}
__attribute__((weak)) void hid_note_vendor(uint16_t usage_page) {
    (void)usage_page;
}

static uint8_t hid_if_id(uint8_t dev_addr, uint8_t instance) {
    return (uint8_t)((dev_addr * 16) + instance);
}

static uint8_t hid_q_next(uint8_t i, uint8_t n) {
    i++;
    if (i >= n) {
        i = 0;
    }
    return i;
}

static uint16_t hid_desc_copy_len(uint8_t const* desc, uint16_t len) {
    if (desc == NULL) {
        return 0;
    }
    if (len > HID_DESC_MAX) {
        return HID_DESC_MAX;
    }
    return len;
}

static void hid_unmount_enqueue(uint8_t if_id) {
    uint8_t tail = hid_unmount_tail;
    uint8_t next = hid_q_next(tail, HID_UNMOUNT_QUEUE);
    int     wait = 0;
    while (next == hid_unmount_head && wait++ < HID_EVENT_WAIT_US) {
        busy_wait_us(1);
    }
    if (next != hid_unmount_head) {
        hid_unmount_ids[tail]   = if_id;
        hid_unmount_epoch[tail] = hid_if_epoch[if_id];
        __compiler_memory_barrier();
        hid_unmount_tail = next;
    } else if (!hid_unmount_overflow) {
        /* Core1 must not mutate the HID table; core0 deletes on the next scan.
         * Do not clobber a slot core0 may already be reading. */
        hid_unmount_overflow_id    = if_id;
        hid_unmount_overflow_epoch = hid_if_epoch[if_id];
        __compiler_memory_barrier();
        hid_unmount_overflow = true;
    }
}

static void hid_note_unmount(uint8_t if_id, uint8_t epoch) {
    /* Use the last parsed type, not the table: a same-scan remount bumps epoch
     * so delete is skipped, but leftover scroll/lock must still reset. */
    if (hid_if_mouse[if_id]) {
        hid_mouse_need_reset = true;
        hid_if_mouse[if_id]  = false;
    }
    /* Its keys would otherwise stay down while another device (a mouse on
     * the same hub) keeps the Quantizer connected. */
    if (hid_if_keyboard[if_id]) {
        hid_keyboard_need_release = true;
        hid_if_keyboard[if_id]    = false;
    }
    if (epoch == hid_if_epoch[if_id]) {
        delete_hid_device(if_id);
    }
}

static void hid_unmount_drain(void) {
    while (hid_unmount_head != hid_unmount_tail) {
        uint8_t h = hid_unmount_head;
        __compiler_memory_barrier();
        uint8_t if_id = hid_unmount_ids[h];
        uint8_t epoch = hid_unmount_epoch[h];
        hid_note_unmount(if_id, epoch);
        __compiler_memory_barrier();
        hid_unmount_head = hid_q_next(h, HID_UNMOUNT_QUEUE);
    }
    if (hid_unmount_overflow) {
        __compiler_memory_barrier();
        uint8_t if_id = hid_unmount_overflow_id;
        uint8_t epoch = hid_unmount_overflow_epoch;
        __compiler_memory_barrier();
        hid_unmount_overflow = false;
        hid_note_unmount(if_id, epoch);
    }
}

static void hid_parse_mounted_descriptor(uint8_t if_id, uint8_t const* desc, uint16_t len) {
    parse_report_descriptor(if_id, desc, len);
    hid_if_mouse[if_id]    = hid_interface_is_mouse(if_id);
    hid_if_keyboard[if_id] = hid_interface_is_keyboard(if_id);
}

static void hid_mount_enqueue(uint8_t if_id, uint8_t const* desc, uint16_t len) {
    uint8_t epoch = hid_if_epoch[if_id];
    uint8_t tail  = hid_mount_tail;
    uint8_t next  = hid_q_next(tail, HID_MOUNT_QUEUE);
    int     wait  = 0;
    while (next == hid_mount_head && wait++ < HID_EVENT_WAIT_US) {
        busy_wait_us(1);
    }
    if (next != hid_mount_head) {
        len = hid_desc_copy_len(desc, len);
        hid_mount_ids[tail]   = if_id;
        hid_mount_len[tail]   = len;
        hid_mount_epoch[tail] = epoch;
        if (len > 0) {
            memcpy(hid_mount_desc[tail], desc, len);
        }
        __compiler_memory_barrier();
        hid_mount_tail = next;
    } else if (epoch == hid_if_epoch[if_id] && !hid_mount_overflow) {
        /* Core1 must not parse; core0 installs the descriptor on the next scan.
         * Do not clobber a descriptor core0 may already be reading. */
        len = hid_desc_copy_len(desc, len);
        hid_mount_overflow_id    = if_id;
        hid_mount_overflow_len   = len;
        hid_mount_overflow_epoch = epoch;
        if (len > 0) {
            memcpy(hid_mount_overflow_desc, desc, len);
        }
        __compiler_memory_barrier();
        hid_mount_overflow = true;
    }
}

static void hid_mount_drain(void) {
    while (hid_mount_head != hid_mount_tail) {
        uint8_t h = hid_mount_head;
        __compiler_memory_barrier();
        uint8_t if_id = hid_mount_ids[h];
        if (hid_mount_epoch[h] == hid_if_epoch[if_id]) {
            hid_parse_mounted_descriptor(if_id, hid_mount_desc[h], hid_mount_len[h]);
        }
        __compiler_memory_barrier();
        hid_mount_head = hid_q_next(h, HID_MOUNT_QUEUE);
    }
    if (hid_mount_overflow) {
        uint8_t  desc[HID_DESC_MAX];
        uint8_t  if_id;
        uint8_t  epoch;
        uint16_t len;
        __compiler_memory_barrier();
        if_id = hid_mount_overflow_id;
        epoch = hid_mount_overflow_epoch;
        len   = hid_mount_overflow_len;
        if (len > HID_DESC_MAX) {
            len = HID_DESC_MAX;
        }
        if (len > 0) {
            memcpy(desc, hid_mount_overflow_desc, len);
        }
        __compiler_memory_barrier();
        hid_mount_overflow = false;
        if (epoch == hid_if_epoch[if_id]) {
            hid_parse_mounted_descriptor(if_id, desc, len);
        }
    }
}

/* QMK compares the matrix once per scan, so a key or button that went down
 * and up again within one scan would never be seen. Once a report has changed
 * the matrix from where this scan started, the rest waits for the next scan.
 * Reports that change nothing in it (cursor motion, a held key repeated) keep
 * being drained, so motion is not held back. */
static matrix_row_t scan_start[MATRIX_ROWS];

static bool matrix_moved(void) {
    return memcmp(scan_start, matrix_dest, sizeof(scan_start)) != 0;
}

/* Drains the RX ring up to `end`, which the caller snapshots from hid_rx_tail
 * so core1 cannot extend the run mid-drain, stopping early once the matrix
 * moved. Returns whether a report was parsed that reports a change. */
static bool hid_rx_drain_to(uint8_t end) {
    bool changed = false;
    while (hid_rx_head != end && !matrix_moved()) {
        uint8_t h = hid_rx_head;
        __compiler_memory_barrier();
        uint8_t if_id = hid_rx_inst[h];
        if (hid_rx_epoch[h] == hid_if_epoch[if_id] && parse_report(if_id, hid_rx_buf[h], hid_rx_len[h])) {
            changed = true;
        }
        __compiler_memory_barrier();
        hid_rx_head = hid_q_next(h, HID_RX_QUEUE);
    }
    return changed;
}

void matrix_init_custom(void) {
    // Configure LED pin
    gpio_set_pin_output_push_pull(KQ_PIN_LED);
    gpio_write_pin_high(KQ_PIN_LED);
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool matrix_has_changed = false;
    matrix_dest             = current_matrix;

    hid_unmount_drain();
    hid_mount_drain();

    /* USB host is core1. Reset leftover scroll/gesture/lock here on core0
     * before RX so a remount in this scan still delivers its first reports.
     * Do not drop the RX queue: leftover packets fail the epoch check. */
    if (hid_disconnect_flag) {
        hid_disconnect_flag  = false;
        hid_mouse_lost_flag  = false;
        hid_mouse_need_reset = false;
        mouse_on_host_disconnect();
        for (uint8_t rowIdx = 0; rowIdx < MATRIX_ROWS; rowIdx++) {
            if (current_matrix[rowIdx] != 0) {
                matrix_has_changed     = true;
                current_matrix[rowIdx] = 0;
            }
        }
    } else if (hid_mouse_need_reset || (hid_mouse_lost_flag && !hid_has_mouse())) {
        hid_mouse_lost_flag  = false;
        hid_mouse_need_reset = false;
        mouse_on_host_disconnect();
        matrix_has_changed = true;
    } else {
        /* Keyboard unplug while a mouse stays: do not leave this set for a
         * later parse gap to look like "mouse gone". */
        hid_mouse_lost_flag = false;
    }
    if (hid_keyboard_need_release) {
        /* A keyboard went away: release its keys like an empty report would
         * (mouse buttons on the shared rows are put back by the hook). */
        hid_keyboard_need_release = false;
        keyboard_parse_result_t none = {0};
        keyboard_report_hook(&none);
        matrix_has_changed = true;
    }
    memcpy(scan_start, current_matrix, sizeof(scan_start));

    /* Relative mouse deltas must not be overwritten. Drain every queued report,
     * but stop at the tail as it was on entry: the overflow slot was filled
     * while the ring was full, so it belongs after those and before anything
     * core1 enqueues once draining frees space. Draining the whole ring first
     * would let a newer button report be applied before an older one. The
     * overflow slot waits while ring entries before it are still pending. */
    uint8_t first_end = hid_rx_tail;
    if (hid_rx_drain_to(first_end)) {
        matrix_has_changed = true;
    }
    uint16_t overflow_seq = hid_rx_overflow_seq;
    if (hid_rx_head == first_end && !matrix_moved() && (overflow_seq & 1u) == 0 && overflow_seq != hid_rx_overflow_taken) {
        uint8_t buf[64];
        uint8_t if_id;
        uint8_t epoch;
        uint8_t len;
        __compiler_memory_barrier();
        if_id = hid_rx_overflow_inst;
        epoch = hid_rx_overflow_epoch;
        len   = hid_rx_overflow_len;
        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }
        if (len > 0) {
            memcpy(buf, hid_rx_overflow_buf, len);
        }
        __compiler_memory_barrier();
        /* A changed sequence means core1 replaced the slot while we copied, so
         * this copy may be torn. Drop it; the newer report is taken next scan. */
        if (hid_rx_overflow_seq == overflow_seq) {
            hid_rx_overflow_taken = overflow_seq;
            if (epoch == hid_if_epoch[if_id] && parse_report(if_id, buf, len)) {
                matrix_has_changed = true;
            }
        }
    }
    /* Anything core1 enqueued while we were draining, in order after the
     * overflow report. */
    if (hid_rx_drain_to(hid_rx_tail)) {
        matrix_has_changed = true;
    }
    /* Always flush wheel batches. parse_report() is false for vendor reports
     * that still queued analog pan via mouse_report_hook(). */
    mouse_scan_end();

    return matrix_has_changed;
}

void housekeeping_task_kb(void) {
    // Control keyboard indicator LED
    static uint8_t keyboard_led;
    if (keyboard_led != host_keyboard_leds()) {
        uint8_t led_backup = keyboard_led;
        keyboard_led       = host_keyboard_leds();
        if (!send_led_report(&keyboard_led)) {
            keyboard_led = led_backup;
        }
    }

    // Blink LED when USB reports are received
    if (led_count >= 0) {
        if (timer_elapsed(led_count) < LED_BLINK_TIME_MS) {
            gpio_write_pin_low(KQ_PIN_LED);
        } else if (timer_elapsed(led_count) < 2 * LED_BLINK_TIME_MS) {
            gpio_write_pin_high(KQ_PIN_LED);
        } else {
            led_count = -1;
        }
    }
    /* housekeeping_task_user() is invoked from quantum/keyboard.c after this. */
}

static bool send_led_report(uint8_t* leds) {
    if (kbd_addr != 0) {
        return tuh_hid_set_report(kbd_addr, kbd_instance, 0, HID_REPORT_TYPE_OUTPUT, leds, sizeof(*leds));
    }

    return false;
}

void tuh_mount_cb(uint8_t dev_addr) {
    dprintf("USB device is mounted:%d\n", dev_addr);

    if (led_count < 0) {
        led_count = timer_read();
    }

    for (int instance = 0; instance < tuh_hid_instance_count(dev_addr); instance++) {
        uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            kbd_addr     = dev_addr;
            kbd_instance = instance;
        }
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    uint8_t if_id = hid_if_id(dev_addr, instance);
    dprintf("HID is mounted:%d:%d\n", dev_addr, instance);
    hid_if_epoch[if_id]++;
    hid_mount_enqueue(if_id, desc_report, desc_len);
    if (hid_mounted_count < 255) {
        hid_mounted_count++;
    }
    /* Leave hid_disconnect_flag set so a remount before core0 runs still
     * resets leftover scroll/gesture/lock from the previous device. */
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    uint8_t if_id = hid_if_id(dev_addr, instance);
    dprintf("HID unmounted:%d:%d\n", dev_addr, instance);
    hid_if_epoch[if_id]++;
    hid_unmount_enqueue(if_id);

    if (dev_addr == kbd_addr && instance == kbd_instance) {
        kbd_addr     = 0;
        kbd_instance = 0;
    }
    if (hid_mounted_count > 0) {
        hid_mounted_count--;
    }
    if (hid_mounted_count == 0) {
        hid_disconnect_flag = true;
    }
    hid_mouse_lost_flag = true;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    dprintf("Report received\n");
    if (led_count < 0) {
        led_count = timer_read();
    }

    /* Host stack is core1, matrix scan is core0. Queue relative reports instead
     * of overwriting; wait briefly only if the queue is full. Stale packets
     * after unplug are dropped by epoch, so keep queuing during disconnect. */
    if (len > 0 && report != NULL) {
        uint8_t tail = hid_rx_tail;
        uint8_t next = hid_q_next(tail, HID_RX_QUEUE);
        int     wait = 0;
        while (next == hid_rx_head && wait++ < HID_RX_WAIT_US) {
            busy_wait_us(1);
        }
        if (next != hid_rx_head) {
            if (len > sizeof(hid_rx_buf[0])) {
                len = sizeof(hid_rx_buf[0]);
            }
            uint8_t if_id     = hid_if_id(dev_addr, instance);
            memcpy(hid_rx_buf[tail], report, len);
            hid_rx_len[tail]   = (uint8_t)len;
            hid_rx_inst[tail]  = if_id;
            hid_rx_epoch[tail] = hid_if_epoch[if_id];
            __compiler_memory_barrier();
            hid_rx_tail = next;
        } else {
            /* Queue full: keep the newest report, not the first one that
             * overflowed. A wheel packet after many cursor packets was the one
             * that disappeared. */
            if (len > sizeof(hid_rx_overflow_buf)) {
                len = sizeof(hid_rx_overflow_buf);
            }
            uint8_t if_id = hid_if_id(dev_addr, instance);
            hid_rx_overflow_seq++; /* odd: a write is in progress */
            __compiler_memory_barrier();
            memcpy(hid_rx_overflow_buf, report, len);
            hid_rx_overflow_len   = (uint8_t)len;
            hid_rx_overflow_inst  = if_id;
            hid_rx_overflow_epoch = hid_if_epoch[if_id];
            __compiler_memory_barrier();
            hid_rx_overflow_seq++; /* even: the slot is stable again */
        }
    }

    tuh_hid_receive_report(dev_addr, instance);
}

__attribute__((weak)) void keyboard_report_hook(keyboard_parse_result_t const* report) {
    if (matrix_dest == NULL || report == NULL) {
        return;
    }

    if (debug_enable) {
        uprintf("Keyboard report\n");
        for (int idx = 0; idx < sizeof(report->bits); idx++) {
            uprintf("%02X ", report->bits[idx]);
        }
        uprintf("\n");
    }

    for (uint8_t rowIdx = 0; rowIdx < MATRIX_ROWS - 2; rowIdx++) {
        matrix_dest[rowIdx + 1] = report->bits[rowIdx];
    }

    // copy modifier bits
    matrix_dest[0]               = report->bits[28];
    matrix_dest[29]              = 0;
    /* Row 31 is orange gesture slots; HID 0xF0-0xF7 must not activate them. */
    matrix_dest[MATRIX_ROWS - 1] = 0;

    /* Mouse buttons share keyboard matrix rows; re-apply after this overwrite. */
    keyboard_report_post_hook();
}

__attribute__((weak)) void mouse_report_hook(mouse_parse_result_t const* report) {
    if (report == NULL) {
        return;
    }
    if (debug_enable) {
        uprintf("Mouse report\n");
        uprintf("b:%d ", report->button);
        uprintf("x:%d ", report->x);
        uprintf("y:%d ", report->y);
        uprintf("v:%d ", report->v);
        uprintf("h:%d ", report->h);
        uprintf("undef:%u\n", report->undefined);
    }

    mouse_send_flag = true;

    report_mouse_t mouse = pointing_device_get_report();

    if (report->has_button) {
        mouse.buttons = report->button;
    }

    mouse.x += report->x;
    mouse.y += report->y;
    mouse.v += report->v;
    mouse.h += report->h;

    pointing_device_set_report(mouse);
}

bool pointing_device_task(void) {
    if (mouse_send_flag) {
        /* Shared mouse EP: last report wins. Rebuild buttons from mousekey + keymap locks. */
        report_mouse_t mouse = pointing_device_get_report();
        mouse_merge_buttons(&mouse);
        /* Wheel output waits outside the report; take this report's share. */
        mouse_wheel_flush(&mouse);
        pointing_device_set_report(mouse);
        bool send_report = pointing_device_send();
        mouse_send_flag  = false;
        mouse_after_send();
        return send_report;
    }

    return false;
}

void vendor_report_parser(uint16_t usage_page, hid_report_member_t const* member, uint8_t const* data, uint8_t len) {
    (void)member;
    hid_note_vendor(usage_page);
    // For Lenovo thinkpad keyboard(17ef:6047)
    if (usage_page == 0xFFA1 && data != NULL && len > 0) {
        mouse_parse_result_t mouse = {0};
        mouse.h                    = (data[0] & 0x80 ? 0xFF00 : 0) | data[0];
        mouse_report_hook(&mouse);
    }
}

__attribute__((weak)) void system_report_hook(uint16_t report) {
    hid_note_system(report);
    host_system_send(report);
    wait_ms(TAP_CODE_DELAY);
    host_system_send(0);
}

__attribute__((weak)) void consumer_report_hook(uint16_t report) {
    hid_note_consumer(report);
    host_consumer_send(report);
    wait_ms(TAP_CODE_DELAY);
    host_consumer_send(0);
}
