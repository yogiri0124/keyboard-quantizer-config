// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#define EMBEDDED_CLI_IMPL
#include "embedded_cli.h"

#include QMK_KEYBOARD_H
#include "debug.h"
#include "bootloader.h"
#include "pio_usb_ll.h"
#include "quantizer_mouse.h"
#include "keymap.h"

#include <string.h>

extern void tusb_print_debug_buffer(void);

#define CLI_BUFFER_SIZE 1024
static CLI_UINT           cliBuffer[BYTES_TO_CLI_UINTS(CLI_BUFFER_SIZE)];
static EmbeddedCli       *cli             = NULL;

void virtser_recv(uint8_t c) {
    if (cli != NULL) {
        embeddedCliReceiveChar(cli, c);
    }
}

static void writeChar(EmbeddedCli *_, char c) { printf("%c", c); }

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
        printf("ovr %s\n", user_os_override_name(user_os_override_get()));
        return;
    }
    const char *cmd = embeddedCliGetToken(args, 1);
    if (cmd == NULL) {
        ovr_cli_usage();
        return;
    }
    if (strcmp(cmd, "off") == 0) {
        user_os_override_set(KEY_OS_OVERRIDE_DISABLE);
    } else if (strcmp(cmd, "uj") == 0) {
        user_os_override_set(US_KEY_JP_OS_OVERRIDE_DISABLE);
    } else if (strcmp(cmd, "ju") == 0) {
        user_os_override_set(JP_KEY_US_OS_OVERRIDE_DISABLE);
    } else {
        ovr_cli_usage();
        return;
    }
    printf("ovr %s\n", user_os_override_name(user_os_override_get()));
}

static void mouse_cli_usage(void) {
    printf("mouse\n");
    printf("mouse reset\n");
    printf("mouse scroll <%u-%u>\n", (unsigned)USER_SCROLL_DIV_MIN, (unsigned)USER_SCROLL_DIV_MAX);
    printf("mouse dpi <%u-%u>\n", (unsigned)USER_CURSOR_SCALE_MIN, (unsigned)USER_CURSOR_SCALE_MAX);
    printf("mouse thresh <%u-%u>\n", (unsigned)USER_GESTURE_THRESHOLD_MIN, (unsigned)USER_GESTURE_THRESHOLD_MAX);
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
        mouse_config_reset();
        mouse_cli_print();
        return;
    }
    uint8_t val;
    if (n < 2 || !parse_u8_token(embeddedCliGetToken(args, 2), &val)) {
        mouse_cli_usage();
        return;
    }
    if (mouse_set_named_value(cmd, val)) {
        mouse_cli_print();
        return;
    }
    mouse_cli_usage();
}

static void onHid(EmbeddedCli *cli, char *args, void *context) {
    (void)cli;
    (void)context;
    const char *cmd = embeddedCliGetToken(args, 1);
    if (cmd != NULL && strcmp(cmd, "desc") == 0) {
        hid_cli_print_desc();
        return;
    }
    hid_cli_print();
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
    cli = embeddedCliNew(config);
    if (cli == NULL) {
        return;
    }
    cli->writeChar = writeChar;
    embeddedCliAddBinding(
        cli, (CliCommandBinding){"version", "Show version", false, NULL,
                                 onVersion});
    embeddedCliAddBinding(
        cli, (CliCommandBinding){"piousb", "Pio usb status", false, NULL,
                                 onPioUsbStatus});
    embeddedCliAddBinding(cli, (CliCommandBinding){"dfu", "jump to bootloader",
                                                   false, NULL, onDfu});
    embeddedCliAddBinding(
        cli, (CliCommandBinding){"debug", "toggle debug option", false, NULL,
                                 onDebug});
    embeddedCliAddBinding(
        cli, (CliCommandBinding){"mouse", "Show/set mouse settings", true, NULL,
                                 onMouse});
    embeddedCliAddBinding(
        cli, (CliCommandBinding){"ovr", "JP/US key override: off|uj|ju", true, NULL,
                                 onOvr});
    embeddedCliAddBinding(
        cli, (CliCommandBinding){"hid", "HID report counts; hid desc", true,
                                 NULL, onHid});
}

void cli_exec(void) {
    if (cli != NULL) {
        embeddedCliProcess(cli);
    }

    tusb_print_debug_buffer();
}