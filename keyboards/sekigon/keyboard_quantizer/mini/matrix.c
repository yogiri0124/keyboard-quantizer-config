// Copyright 2023 sekigon-gonnoc
// Copyright 2026 yogiri0124
// SPDX-License-Identifier: GPL-2.0-or-later

/* QMK side of the board: the matrix scan reads what the USB host (core1)
 * queued, see hid_input.h; media keys go straight to the host. */

#include QMK_KEYBOARD_H

#include "hid_input.h"
#include "hid_queue.h"

_Static_assert(MATRIX_ROWS == KQ_MATRIX_ROWS && MATRIX_COLS == 8, "identity matrix is 32 x 8");
_Static_assert(sizeof(matrix_row_t) == 1, "one byte per matrix row");
_Static_assert(KC_MS_BTN1 == 0xD1, "hid_input.c places mouse buttons at KC_MS_BTN1");

extern void busy_wait_us(uint64_t delay_us);

void hidq_barrier(void) {
    __asm__ volatile("dmb" ::: "memory");
}

void hidq_wait_us(uint32_t us) {
    busy_wait_us(us);
}

void kq_host_consumer(uint16_t usage) {
    host_consumer_send(usage);
}

void kq_host_system(uint16_t usage) {
    host_system_send(usage);
}

void matrix_init_custom(void) {}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    return hid_input_scan(current_matrix);
}
