// Copyright 2023 sekigon-gonnoc
// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* TinyUSB host callbacks. They run on core1, so they only queue (hid_queue.h)
 * and never touch core0 state. Also the status LED and the keyboard's lock
 * LEDs (Caps Lock etc.), which run on core0. */

#include QMK_KEYBOARD_H

#include "tusb.h"
#include "hid_queue.h"

#define KQ_PIN_LED 7
#define LED_BLINK_MS 50

static volatile uint8_t  kbd_addr;
static volatile uint8_t  kbd_instance;
static volatile bool     activity;      /* set by core1 on mount / report */
static uint16_t          blink_start;   /* core0 */
static bool              blinking;      /* core0 */

static uint8_t interface_id(uint8_t dev_addr, uint8_t instance) {
    return (uint8_t)(dev_addr * 16 + instance);
}

void tuh_mount_cb(uint8_t dev_addr) {
    activity = true;
    for (uint8_t instance = 0; instance < tuh_hid_instance_count(dev_addr); instance++) {
        if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
            kbd_addr     = dev_addr;
            kbd_instance = instance;
        }
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    hidq_push_mount(interface_id(dev_addr, instance), desc_report, desc_len);
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    hidq_push_unmount(interface_id(dev_addr, instance));
    if (dev_addr == kbd_addr && instance == kbd_instance) {
        kbd_addr     = 0;
        kbd_instance = 0;
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    activity = true;
    hidq_push_report(interface_id(dev_addr, instance), report, len);
    tuh_hid_receive_report(dev_addr, instance);
}

void matrix_init_kb(void) {
    gpio_set_pin_output_push_pull(KQ_PIN_LED);
    gpio_write_pin_high(KQ_PIN_LED);
    matrix_init_user();
}

/* The connected keyboard shows the host's Caps Lock / Num Lock state. */
static void sync_lock_leds(void) {
    static uint8_t sent;
    uint8_t        leds = host_keyboard_leds();
    if (leds == sent || kbd_addr == 0) {
        return;
    }
    uint8_t report = leds;
    if (tuh_hid_set_report(kbd_addr, kbd_instance, 0, HID_REPORT_TYPE_OUTPUT, &report, sizeof(report))) {
        sent = leds;
    }
}

/* One short blink (low = on) when USB input arrives, then off for as long
 * again, so steady input reads as a steady blink. */
static void blink_status_led(void) {
    if (!blinking && activity) {
        activity    = false;
        blinking    = true;
        blink_start = timer_read();
    }
    if (!blinking) {
        return;
    }
    uint16_t t = timer_elapsed(blink_start);
    if (t < LED_BLINK_MS) {
        gpio_write_pin_low(KQ_PIN_LED);
    } else if (t < 2 * LED_BLINK_MS) {
        gpio_write_pin_high(KQ_PIN_LED);
    } else {
        blinking = false;
    }
}

void housekeeping_task_kb(void) {
    sync_lock_leds();
    blink_status_led();
}
