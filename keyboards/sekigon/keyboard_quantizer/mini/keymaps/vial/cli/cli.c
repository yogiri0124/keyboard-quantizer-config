// Copyright 2023 sekigon-gonnoc
// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#define EMBEDDED_CLI_IMPL
#include "embedded_cli.h"

#include QMK_KEYBOARD_H
#include "debug.h"
#include "bootloader.h"
#include "pio_usb_ll.h"
#include "hid_desc.h"
#include "hid_input.h"
#include "hid_queue.h"
#include "mouse.h"
#include "scroll_out.h"
#include "settings.h"

#include <string.h>

extern void tusb_print_debug_buffer(void);

#define CLI_BUFFER_SIZE 1024
static CLI_UINT     cliBuffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];
static EmbeddedCli *cli = NULL;

void virtser_recv(uint8_t c) {
    if (cli != NULL) {
        embeddedCliReceiveChar(cli, c);
    }
}

static void writeChar(EmbeddedCli *_, char c) {
    printf("%c", c);
}

static void onVersion(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)args;
    (void)context;
    printf("firmware version: %s\n", STR(GIT_DESCRIBE));
}

static void onDfu(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)args;
    (void)context;
    bootloader_jump();
}

static void onDebug(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)args;
    (void)context;
    debug_enable = !debug_enable;
    printf("debug %s\n", debug_enable ? "on" : "off");
}

static void onPioUsbStatus(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)args;
    (void)context;
    pio_port_t const *pp = PIO_USB_PIO_PORT(0);
    unsigned long     tx = (unsigned long)pp->total_transaction_count;
    unsigned long     er = (unsigned long)pp->total_error_count;
    unsigned long     fe = (unsigned long)pp->total_fatal_error_count;
    if (tx == 0) {
        printf("error rate: %lu / 0\n", er);
        printf("fatal error rate: %lu / 0\n", fe);
        return;
    }
    printf("error rate: %lu / %lu = %lu%%\n", er, tx, (er * 100ul) / tx);
    printf("fatal error rate: %lu / %lu = %lu%%\n", fe, tx, (fe * 100ul) / tx);
}

static void ovr_cli_usage(void) {
    printf("ovr\n");
    printf("ovr off\n");
    printf("ovr uj\n");
    printf("ovr ju\n");
}

static void onOvr(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)context;
    uint16_t n = embeddedCliGetTokenCount(args);
    if (n == 0) {
        printf("ovr %s\n", os_override_name(os_override_get()));
        return;
    }
    const char *cmd = embeddedCliGetToken(args, 1);
    if (cmd == NULL) {
        ovr_cli_usage();
        return;
    }
    if (strcmp(cmd, "off") == 0) {
        os_override_set(OS_OVERRIDE_OFF);
    } else if (strcmp(cmd, "uj") == 0) {
        os_override_set(OS_OVERRIDE_US_ON_JP);
    } else if (strcmp(cmd, "ju") == 0) {
        os_override_set(OS_OVERRIDE_JP_ON_US);
    } else {
        ovr_cli_usage();
        return;
    }
    printf("ovr %s\n", os_override_name(os_override_get()));
}

static void mouse_cli_usage(void) {
    printf("mouse\n");
    printf("mouse reset\n");
    for (uint8_t s = 0; s < SETTING_COUNT; s++) {
        printf("mouse %s <%u-%u>\n", settings_name((setting_t)s), (unsigned)settings_min((setting_t)s),
               (unsigned)settings_max((setting_t)s));
    }
}

static bool parse_u8_token(const char *s, uint8_t *out) {
    if (s == NULL || *s == '\0') {
        return false;
    }
    unsigned v = 0;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        v = v * 10u + (unsigned)(*p - '0');
        if (v > 255u) {
            return false;
        }
    }
    *out = (uint8_t)v;
    return true;
}

static void onMouse(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)context;
    uint16_t n = embeddedCliGetTokenCount(args);
    if (n == 0) {
        mouse_cli_print();
        return;
    }
    const char *cmd = embeddedCliGetToken(args, 1);
    if (cmd == NULL) {
        mouse_cli_usage();
        return;
    }
    if (strcmp(cmd, "reset") == 0) {
        mouse_reset_settings_and_latches();
        mouse_cli_print();
        return;
    }
    uint8_t val;
    if (n < 2 || !parse_u8_token(embeddedCliGetToken(args, 2), &val)) {
        mouse_cli_usage();
        return;
    }
    if (settings_set_by_name(cmd, val)) {
        mouse_cli_print();
        return;
    }
    mouse_cli_usage();
}

static const char *kind_name(hid_kind_t k) {
    switch (k) {
        case HID_KIND_KEYBOARD:
            return "keyboard";
        case HID_KIND_MOUSE:
            return "mouse";
        case HID_KIND_CONSUMER:
            return "consumer";
        case HID_KIND_SYSTEM:
            return "system";
        case HID_KIND_OTHER:
            break;
    }
    return "other";
}

/* The compiled descriptors: per interface, each input report and its
 * fields (bit offset, size x count, flags C=constant V=variable R=relative,
 * usage page, usages, logical range). */
static void print_descriptors(void) {
    uint8_t found = 0;
    for (uint8_t i = 0; i < HID_DESC_DEVICES; i++) {
        const hid_device_t *dev = hid_desc_device_at(i);
        if (dev == NULL) {
            continue;
        }
        found++;
        printf("if:%u%s\n", (unsigned)dev->interface, dev->truncated ? " (table full: some fields left out)" : "");
        for (uint8_t r = 0; r < dev->report_count; r++) {
            const hid_report_t *rep = &dev->reports[r];
            printf("  id:%u %s page:0x%04X usage:0x%04X bits:%u\n", (unsigned)rep->id, kind_name(rep->kind),
                   (unsigned)rep->collection_page, (unsigned)rep->collection_usage, (unsigned)rep->bit_length);
            for (uint8_t f = 0; f < dev->field_count; f++) {
                const hid_field_t *fd = &dev->fields[f];
                if (fd->report != r) {
                    continue;
                }
                printf("    @%u %ux%u %c%c%c page:0x%04X", (unsigned)fd->bit_offset, (unsigned)fd->bit_size,
                       (unsigned)fd->count, (fd->flags & HID_FIELD_CONSTANT) ? 'C' : '-',
                       (fd->flags & HID_FIELD_VARIABLE) ? 'V' : '-', (fd->flags & HID_FIELD_RELATIVE) ? 'R' : '-',
                       (unsigned)fd->usage_page);
                if (fd->list_len > 0) {
                    printf(" usages:");
                    for (uint8_t u = 0; u < fd->list_len; u++) {
                        printf("%s0x%04X", u ? "," : "", (unsigned)dev->usages[fd->list_start + u]);
                    }
                } else {
                    printf(" usage:0x%04X-0x%04X", (unsigned)fd->usage_min, (unsigned)fd->usage_max);
                }
                printf(" logical:%ld..%ld\n", (long)fd->logical_min, (long)fd->logical_max);
            }
        }
    }
    if (found == 0) {
        printf("no HID devices\n");
    }
}

static void print_hid_stats(void) {
    hid_input_stats_t s = hid_input_stats();
    hidq_stats_t      q = hidq_stats();
    printf("kb %lu  mouse %lu  consumer %lu last=0x%04X  system %lu last=0x%04X\n", (unsigned long)s.keyboard,
           (unsigned long)s.mouse, (unsigned long)s.consumer, (unsigned)s.last_consumer, (unsigned long)s.system,
           (unsigned)s.last_system);
    printf("other %lu last page=0x%04X  buttons 0x%04X  undefined 0x%04X  unknown %lu\n", (unsigned long)s.other,
           (unsigned)s.last_other_page, (unsigned)s.last_buttons, (unsigned)s.last_undefined,
           (unsigned long)s.unknown_reports);
    /* HID and QMK both mean "up" and "right" by a positive value; the ELECOM
     * HUGE PLUS sends a left tilt as positive pan (see wheel.c). */
    printf("wheel %d  pan %d  (last non-zero, as the device sent them)\n", (int)s.last_wheel, (int)s.last_pan);
    /* What the PC asked for: x1 until Windows/Linux enable high resolution. */
    printf("host wheel x%ld  pan x%ld\n", (long)scroll_out_host_multiplier(true),
           (long)scroll_out_host_multiplier(false));
    printf("queue reports %lu  overflow %lu  lost events %lu\n", (unsigned long)q.pushed_reports,
           (unsigned long)q.slot_reports, (unsigned long)q.dropped_events);
}

static void onHid(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)context;
    const char *cmd = embeddedCliGetToken(args, 1);
    if (cmd != NULL && strcmp(cmd, "desc") == 0) {
        print_descriptors();
        return;
    }
    print_hid_stats();
    if (cmd != NULL) {
        printf("hid desc  dump parsed report descriptor\n");
    }
}

void cli_init(void) {
    EmbeddedCliConfig *config  = embeddedCliDefaultConfig();
    config->cliBuffer          = cliBuffer;
    config->cliBufferSize      = CLI_BUFFER_SIZE;
    config->enableAutoComplete = false;
    config->historyBufferSize  = 64;
    config->maxBindingCount    = 16;
    cli                        = embeddedCliNew(config);
    if (cli == NULL) {
        return;
    }
    cli->writeChar                            = writeChar;
    static const CliCommandBinding commands[] = {
        {"version", "Show version", false, NULL, onVersion},
        {"piousb", "Pio usb status", false, NULL, onPioUsbStatus},
        {"dfu", "jump to bootloader", false, NULL, onDfu},
        {"debug", "toggle debug option", false, NULL, onDebug},
        {"mouse", "Show/set mouse settings", true, NULL, onMouse},
        {"ovr", "JP/US key override: off|uj|ju", true, NULL, onOvr},
        {"hid", "HID report counts; hid desc", true, NULL, onHid},
    };
    for (uint8_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        embeddedCliAddBinding(cli, commands[i]);
    }
}

void cli_exec(void) {
    if (cli != NULL) {
        embeddedCliProcess(cli);
    }

    tusb_print_debug_buffer();
}