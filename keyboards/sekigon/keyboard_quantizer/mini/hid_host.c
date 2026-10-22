// Copyright 2023 sekigon-gonnoc
// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* TinyUSB host callbacks, the keyboard's lock LEDs and the status LED.
 *
 * The callbacks run on core1 with the USB host stack. They queue what
 * arrives (hid_queue.h) and never touch core0 state; core0 only publishes
 * the lock LED state it wants, and core1 sends it, since TinyUSB's host API
 * is not safe to call from the other core. */

#include QMK_KEYBOARD_H

#include "tusb.h"
#include "hid_queue.h"

#define KQ_PIN_LED 7
#define LED_BLINK_MS 50

/* Core1. */
static uint8_t kbd_addr; /* the keyboard interface that shows lock LEDs, 0: none */
static uint8_t kbd_instance;
static uint8_t led_sent;
static bool    led_known; /* led_sent reached the current keyboard */
/* The output report goes out asynchronously and TinyUSB keeps a pointer to
 * it until done: it lives here and is not touched while led_busy. */
static uint8_t       led_report;
static volatile bool led_busy;

/* Core0 -> core1. */
static volatile uint8_t led_wanted;
/* Core1 -> core0. */
static volatile bool activity;

/* Core0. */
static uint16_t blink_start;
static bool     blinking;

static uint8_t interface_id(uint8_t dev_addr, uint8_t instance) {
    return (uint8_t)(dev_addr * 16 + instance);
}

/* Core1: brings the keyboard's lock LEDs to what core0 wants. Called from
 * every callback; mice report often enough for an LED change to go out at
 * once. */
static void sync_lock_leds(void) {
    uint8_t wanted = led_wanted;
    if (kbd_addr == 0 || led_busy || (led_known && wanted == led_sent)) {
        return;
    }
    led_report = wanted;
    led_busy   = true;
    if (tuh_hid_set_report(kbd_addr, kbd_instance, 0, HID_REPORT_TYPE_OUTPUT, &led_report, sizeof(led_report))) {
        led_sent  = wanted;
        led_known = true;
    } else {
        led_busy = false; /* not started: try again with the next callback */
    }
}

void tuh_mount_cb(uint8_t dev_addr) {
    activity = true;
    for (uint8_t instance = 0; instance < tuh_hid_instance_count(dev_addr); instance++) {
        if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
            kbd_addr     = dev_addr;
            kbd_instance = instance;
            led_known    = false; /* a new keyboard: tell it the current state */
            led_busy     = false;
        }
    }
    sync_lock_leds();
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
        led_busy     = false; /* its transfer will not complete */
    }
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t report_type,
                                    uint16_t len) {
    (void)dev_addr;
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)len;
    led_busy = false;
    sync_lock_leds(); /* it may have changed meanwhile */
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    activity = true;
    hidq_push_report(interface_id(dev_addr, instance), report, len);
    tuh_hid_receive_report(dev_addr, instance);
    sync_lock_leds();
}

void matrix_init_kb(void) {
    gpio_set_pin_output_push_pull(KQ_PIN_LED);
    gpio_write_pin_high(KQ_PIN_LED);
    matrix_init_user();
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
    /* The PC's Caps Lock / Num Lock state, for the connected keyboard. */
    led_wanted = host_keyboard_leds();
    blink_status_led();
}
