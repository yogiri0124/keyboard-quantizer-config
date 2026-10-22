// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hid_queue.h"
#include "hid_scan.h"

#include <string.h>

_Static_assert((HIDQ_RING_BYTES & (HIDQ_RING_BYTES - 1)) == 0, "ring size must be a power of two");
_Static_assert(HIDQ_RESERVE_BYTES < HIDQ_RING_BYTES / 2, "reserve must leave room for reports");

#define HEADER_BYTES 4u
#define TYPE_PAD 0
#define MOUNT_WAIT_US 5000u
#define SLOT_READ_TRIES 64

/* Ring positions count bytes from the start and only grow: the difference is
 * the fill level even across uint32 wraparound. */
static uint8_t           ring[HIDQ_RING_BYTES] __attribute__((aligned(4)));
static volatile uint32_t ring_head; /* written by core0 */
static volatile uint32_t ring_tail; /* written by core1 */

typedef struct {
    volatile uint32_t seq; /* odd while core1 writes */
    uint8_t           interface;
    uint8_t           report_id; /* 0 when the device uses none */
    bool              valid;
    uint16_t          len;
    uint32_t          pos;   /* ring position this report belongs at */
    uint32_t          stamp; /* arrival order among slots */
    uint8_t           data[HIDQ_REPORT_MAX];
} slot_t;

static slot_t slots[HIDQ_SLOTS];

/* Core1 only. */
static bool     overflow;
static uint32_t stamp_counter;
static uint8_t  uses_ids[256 / 8]; /* per interface, from its descriptor */
/* Core0 only. */
static uint32_t taken_seq[HIDQ_SLOTS];

static hidq_stats_t stats;

static uint32_t round4(uint32_t n) {
    return (n + 3u) & ~3u;
}

static uint32_t free_bytes(void) {
    return HIDQ_RING_BYTES - (ring_tail - ring_head);
}

static void put_header(uint32_t off, uint8_t type, uint8_t interface, uint16_t len) {
    ring[off]     = type;
    ring[off + 1] = interface;
    ring[off + 2] = (uint8_t)len;
    ring[off + 3] = (uint8_t)(len >> 8);
}

/* Appends one record. reserve: bytes that must stay free afterwards. */
static bool push_record(uint8_t type, uint8_t interface, const uint8_t *data, uint16_t len, uint32_t reserve) {
    uint32_t size  = HEADER_BYTES + round4(len);
    uint32_t tail  = ring_tail;
    uint32_t off   = tail & (HIDQ_RING_BYTES - 1);
    uint32_t room  = HIDQ_RING_BYTES - off; /* contiguous to the end, a multiple of 4 */
    uint32_t pad   = room < size ? room : 0;
    uint32_t avail = HIDQ_RING_BYTES - (tail - ring_head);
    if (avail < size + pad + reserve) {
        return false;
    }
    if (pad != 0) {
        put_header(off, TYPE_PAD, 0, (uint16_t)(pad - HEADER_BYTES));
        off = 0;
    }
    put_header(off, type, interface, len);
    if (len > 0) {
        memcpy(&ring[off + HEADER_BYTES], data, len);
    }
    hidq_barrier();
    ring_tail = tail + pad + size;
    return true;
}

static bool interface_uses_ids(uint8_t interface) {
    return (uses_ids[interface / 8] >> (interface % 8)) & 1;
}

static void set_uses_ids(uint8_t interface, bool on) {
    uint8_t bit             = (uint8_t)(1u << (interface % 8));
    uses_ids[interface / 8] = on ? (uint8_t)(uses_ids[interface / 8] | bit) : (uint8_t)(uses_ids[interface / 8] & ~bit);
}

/* One slot per report (interface and Report ID): each report carries its
 * own state, so a newer mouse report must not replace a consumer release. */
static slot_t *slot_for(uint8_t interface, uint8_t report_id) {
    slot_t *spare  = NULL;
    slot_t *oldest = &slots[0];
    for (uint8_t i = 0; i < HIDQ_SLOTS; i++) {
        slot_t *s = &slots[i];
        if (s->valid && s->interface == interface && s->report_id == report_id) {
            return s;
        }
        if (!s->valid && spare == NULL) {
            spare = s;
        }
        if ((int32_t)(s->stamp - oldest->stamp) < 0) { /* wrap-safe, as in due_slot() */
            oldest = s;
        }
    }
    return spare != NULL ? spare : oldest;
}

static void write_slot(uint8_t interface, const uint8_t *data, uint16_t len) {
    uint8_t id = interface_uses_ids(interface) ? data[0] : 0;
    slot_t *s  = slot_for(interface, id);
    s->seq++;
    hidq_barrier();
    s->interface = interface;
    s->report_id = id;
    s->len       = len;
    s->pos       = ring_tail;
    s->stamp     = ++stamp_counter;
    memcpy(s->data, data, len);
    s->valid = true;
    hidq_barrier();
    s->seq++;
    stats.slot_reports++;
}

void hidq_push_report(uint8_t interface, const uint8_t *data, uint16_t len) {
    if (data == NULL || len == 0) {
        return;
    }
    if (len > HIDQ_REPORT_MAX) {
        len = HIDQ_REPORT_MAX;
    }
    stats.pushed_reports++;
    /* Leave overflow once core0 has caught up by half the ring. */
    if (overflow && free_bytes() >= HIDQ_RING_BYTES / 2) {
        overflow = false;
    }
    if (!overflow) {
        if (push_record(HIDQ_REPORT, interface, data, len, HIDQ_RESERVE_BYTES)) {
            return;
        }
        overflow = true;
    }
    write_slot(interface, data, len);
}

static void push_event(uint8_t type, uint8_t interface, const uint8_t *data, uint16_t len) {
    for (uint32_t waited = 0;; waited++) {
        if (push_record(type, interface, data, len, 0)) {
            return;
        }
        if (waited >= MOUNT_WAIT_US) {
            stats.dropped_events++;
            return;
        }
        hidq_wait_us(1);
    }
}

void hidq_push_mount(uint8_t interface, const uint8_t *desc, uint16_t len) {
    if (desc == NULL) {
        len = 0;
    }
    if (len > HIDQ_DESC_MAX) {
        len = HIDQ_DESC_MAX;
    }
    hid_scan_t scan;
    hid_scan(desc, len, &scan);
    set_uses_ids(interface, scan.uses_report_ids);
    push_event(HIDQ_MOUNT, interface, desc, len);
}

void hidq_push_unmount(uint8_t interface) {
    /* A report still waiting in a slot is older than this unmount, and the
     * device's state goes away with it: drop it, and free the slot. */
    for (uint8_t i = 0; i < HIDQ_SLOTS; i++) {
        slot_t *s = &slots[i];
        if (s->valid && s->interface == interface) {
            s->seq++;
            hidq_barrier();
            s->valid = false;
            hidq_barrier();
            s->seq++;
        }
    }
    set_uses_ids(interface, false);
    push_event(HIDQ_UNMOUNT, interface, NULL, 0);
}

/* The overflow report that is due at ring position head (it arrived before
 * the record there), oldest first. Returns its index, or -1. */
static int due_slot(uint32_t head) {
    int      best       = -1;
    uint32_t best_stamp = 0;
    for (uint8_t i = 0; i < HIDQ_SLOTS; i++) {
        slot_t  *s   = &slots[i];
        uint32_t seq = s->seq;
        if ((seq & 1u) != 0) {
            /* Being written right now: it belongs at the current tail, which
             * is past head unless the ring is empty. Either way it is not
             * older than what is at head, so it can wait. */
            continue;
        }
        if (!s->valid || seq == taken_seq[i] || (int32_t)(head - s->pos) < 0) {
            continue;
        }
        if (best < 0 || (int32_t)(s->stamp - best_stamp) < 0) {
            best       = i;
            best_stamp = s->stamp;
        }
    }
    return best;
}

typedef enum { SLOT_TAKEN, SLOT_CHANGED, SLOT_BUSY } slot_read_t;

/* Copies slot i if, in one consistent snapshot, it is still a pending
 * report due at head. Core1 may have replaced it since due_slot() chose it
 * (a newer report, at a later position): then it must be chosen again, not
 * handed out ahead of older ring records. */
static slot_read_t read_slot(int i, uint32_t head, hidq_event_t *ev) {
    slot_t *s = &slots[i];
    for (int tries = 0; tries < SLOT_READ_TRIES; tries++) {
        uint32_t seq = s->seq;
        if ((seq & 1u) != 0) {
            hidq_wait_us(1);
            continue;
        }
        hidq_barrier();
        bool     valid = s->valid;
        uint32_t pos   = s->pos;
        uint16_t len   = s->len > HIDQ_REPORT_MAX ? HIDQ_REPORT_MAX : s->len;
        ev->type       = HIDQ_REPORT;
        ev->interface  = s->interface;
        ev->len        = len;
        memcpy(ev->data, s->data, len);
        hidq_barrier();
        if (s->seq != seq) {
            continue; /* torn: read again */
        }
        if (!valid || seq == taken_seq[i] || (int32_t)(head - pos) < 0) {
            return SLOT_CHANGED;
        }
        taken_seq[i] = seq;
        return SLOT_TAKEN;
    }
    return SLOT_BUSY;
}

bool hidq_pop(hidq_event_t *ev) {
    for (;;) {
        /* Tail first: a slot written after this read belongs past it. */
        uint32_t tail = ring_tail;
        hidq_barrier();
        uint32_t head = ring_head;
        int      due  = due_slot(head);
        if (due >= 0) {
            slot_read_t r = read_slot(due, head, ev);
            if (r == SLOT_CHANGED) {
                continue;
            }
            /* Busy: core1 keeps rewriting it. Try on the next scan rather
             * than hand out a newer ring record first. */
            return r == SLOT_TAKEN;
        }
        if (head == tail) {
            return false;
        }
        uint32_t off  = head & (HIDQ_RING_BYTES - 1);
        uint8_t  type = ring[off];
        uint16_t len  = (uint16_t)(ring[off + 2] | (ring[off + 3] << 8));
        if (type != TYPE_PAD) {
            uint16_t copy = len > HIDQ_DESC_MAX ? HIDQ_DESC_MAX : len;
            ev->type      = (hidq_type_t)type;
            ev->interface = ring[off + 1];
            ev->len       = copy;
            memcpy(ev->data, &ring[off + HEADER_BYTES], copy);
        }
        hidq_barrier();
        ring_head = head + HEADER_BYTES + round4(len);
        if (type != TYPE_PAD) {
            return true;
        }
    }
}

hidq_stats_t hidq_stats(void) {
    return stats;
}

void hidq_reset(void) {
    ring_head     = 0;
    ring_tail     = 0;
    overflow      = false;
    stamp_counter = 0;
    memset(slots, 0, sizeof(slots));
    memset(taken_seq, 0, sizeof(taken_seq));
    memset(uses_ids, 0, sizeof(uses_ids));
    memset(&stats, 0, sizeof(stats));
}
