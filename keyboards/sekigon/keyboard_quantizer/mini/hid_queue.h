// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* Core1 -> core0 queue for USB host events.
 *
 * The USB host stack runs on core1; the descriptor tables, the matrix and
 * everything QMK run on core0. Mounts, unmounts and reports travel in one
 * byte ring, in the order core1 saw them, so core0 never has to guess which
 * device a report belongs to.
 *
 * The ring keeps room for mounts and unmounts. If reports fill the rest
 * (core0 stuck in a long macro, say), core1 keeps only the newest report per
 * interface in a side slot: a keyboard or button report is the full state,
 * so the newest one is the one that must not be lost, while the cursor motion
 * of the skipped ones is. Each slot remembers where in the ring it belongs,
 * and core0 hands it out exactly there, so order is kept.
 *
 * One producer (core1), one consumer (core0). Pure C plus two hooks for the
 * memory barrier and a short wait, so it runs on the host for tests. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HIDQ_RING_BYTES 16384u /* power of two */
#define HIDQ_RESERVE_BYTES 4096u /* kept free for mount / unmount */
#define HIDQ_SLOTS 8             /* interfaces that can hold an overflow report */
#define HIDQ_REPORT_MAX 64
#define HIDQ_DESC_MAX 256

typedef enum {
    HIDQ_MOUNT = 1,
    HIDQ_UNMOUNT,
    HIDQ_REPORT,
} hidq_type_t;

typedef struct {
    hidq_type_t type;
    uint8_t     interface;
    uint16_t    len;
    uint8_t     data[HIDQ_DESC_MAX];
} hidq_event_t;

typedef struct {
    uint32_t pushed_reports;
    uint32_t slot_reports;   /* reports that went to an overflow slot */
    uint32_t dropped_events; /* mounts / unmounts that found no room (never expected) */
} hidq_stats_t;

/* Core1. */
void hidq_push_report(uint8_t interface, const uint8_t *data, uint16_t len);
/* Mount and unmount may wait a little for room; they are rare. */
void hidq_push_mount(uint8_t interface, const uint8_t *desc, uint16_t len);
void hidq_push_unmount(uint8_t interface);

/* Core0: the next event in order, false when there is none. */
bool hidq_pop(hidq_event_t *ev);

hidq_stats_t hidq_stats(void);
void         hidq_reset(void); /* tests only: both sides idle */

/* Platform hooks (defined by the board code, or by the test). */
void hidq_barrier(void);
void hidq_wait_us(uint32_t us);
