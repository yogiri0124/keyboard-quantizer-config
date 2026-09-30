/* Host tests for the Quantizer's HID parser (keyboards/.../parser): feeds
 * report descriptors and reports, checks what reaches the hooks.
 *
 *   cc -std=gnu11 -Wall -I tools/parser_test/include -o parser_test tools/parser_test/parser_test.c
 */

#include <stdio.h>
#include <string.h>

#include "../../keyboards/sekigon/keyboard_quantizer/parser/report_descriptor_parser.c"
#include "../../keyboards/sekigon/keyboard_quantizer/parser/report_parser.c"

static keyboard_parse_result_t last_kb;
static mouse_parse_result_t    last_mouse;
static uint16_t                consumer[8];
static int                     n_consumer;
static int                     n_kb, n_mouse;

void keyboard_report_hook(keyboard_parse_result_t const *r) {
    last_kb = *r;
    n_kb++;
}
void mouse_report_hook(mouse_parse_result_t const *r) {
    last_mouse = *r;
    n_mouse++;
}
void consumer_report_hook(uint16_t u) {
    if (n_consumer < 8) consumer[n_consumer] = u;
    n_consumer++;
}
void system_report_hook(uint16_t u) {
    consumer_report_hook(u);
}
void vendor_report_parser(uint16_t page, hid_report_member_t const *m, uint8_t const *d, uint8_t l) {
    (void)page, (void)m, (void)d, (void)l;
}

static int failures;
#define CHECK(cond, ...)                              \
    do {                                              \
        if (!(cond)) {                                \
            failures++;                               \
            printf("FAIL %s:%d ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                      \
            printf("\n");                             \
        }                                             \
    } while (0)

static void reset(void) {
    memset(&last_kb, 0, sizeof last_kb);
    memset(&last_mouse, 0, sizeof last_mouse);
    n_consumer = n_kb = n_mouse = 0;
}

static bool kb_has(uint8_t usage) {
    return (last_kb.bits[usage >> 3] >> (usage & 7)) & 1;
}

static void load(uint8_t interface, const uint8_t *d, size_t n) {
    CHECK(parse_report_descriptor(interface, d, (uint16_t)n), "descriptor %u rejected", interface);
}

/* Report ID before the Collection (a Global item): the report's first byte
 * is the ID, not the X axis. */
static void test_report_id_before_collection(void) {
    static const uint8_t d[] = {0x05, 0x01, 0x09, 0x02, 0x85, 0x01, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
                                0x09, 0x30, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
                                0xC0, 0xC0};
    load(1, d, sizeof d);
    reset();
    uint8_t r[] = {0x01, 0x05};
    parse_report(1, r, sizeof r);
    CHECK(n_mouse == 1 && last_mouse.x == 5, "x=%d (want 5), mouse reports %d", last_mouse.x, n_mouse);
}

/* Push/Pop restore the Report ID with the rest of the global state. */
static void test_report_id_push_pop(void) {
    static const uint8_t d[] = {
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, /* ID 2 */
        0xA4,                                           /* push (ID 2) */
        0x85, 0x03, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, /* Y in ID 3 */
        0xB4,                                                                         /* pop: back to ID 2 */
        0x09, 0x30, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,       /* X in ID 2 */
        0xC0};
    load(2, d, sizeof d);
    reset();
    uint8_t r2[] = {0x02, 0x07};
    parse_report(2, r2, sizeof r2);
    CHECK(last_mouse.x == 7 && last_mouse.y == 0, "ID 2: x=%d y=%d (want 7, 0)", last_mouse.x, last_mouse.y);
    reset();
    uint8_t r3[] = {0x03, 0x09};
    parse_report(2, r3, sizeof r3);
    CHECK(last_mouse.y == 9 && last_mouse.x == 0, "ID 3: x=%d y=%d (want 0, 9)", last_mouse.x, last_mouse.y);
}

/* Keyboard array: usage = value - Logical Minimum + Usage Minimum. */
static void test_keyboard_array_offset(void) {
    static const uint8_t d[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0x04, 0x29, 0x1D,
                                0x15, 0x04, 0x25, 0x1D, 0x75, 0x08, 0x95, 0x02, 0x81, 0x00, 0xC0};
    load(3, d, sizeof d);
    reset();
    uint8_t r[] = {0x04, 0x05}; /* A, B */
    parse_report(3, r, sizeof r);
    CHECK(kb_has(0x04) && kb_has(0x05) && !kb_has(0x08) && !kb_has(0x09), "array offset wrong");
    reset();
    uint8_t none[] = {0x00, 0x00}; /* below Logical Minimum: no key */
    parse_report(3, none, sizeof none);
    CHECK(!kb_has(0x00) && !kb_has(0x04), "out-of-range value produced a key");
}

/* The usual boot-like layout still works (modifiers bitmap + 6-key array). */
static void test_keyboard_boot_layout(void) {
    static const uint8_t d[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
                                0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
                                0x75, 0x08, 0x81, 0x01, 0x19, 0x00, 0x29, 0xFF, 0x15, 0x00, 0x26, 0xFF,
                                0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x00, 0xC0};
    load(4, d, sizeof d);
    reset();
    uint8_t r[] = {0x02, 0x00, 0x04, 0x28, 0, 0, 0, 0}; /* LShift, A, Enter */
    parse_report(4, r, sizeof r);
    CHECK(kb_has(0xE1) && kb_has(0x04) && kb_has(0x28) && !kb_has(0x00), "boot layout wrong");
}

/* Modifiers listed one Usage each instead of a Usage Minimum/Maximum. */
static void test_keyboard_listed_usages(void) {
    static const uint8_t d[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x09, 0xE0, 0x09, 0xE1,
                                0x09, 0xE2, 0x09, 0xE3, 0x09, 0xE4, 0x09, 0xE5, 0x09, 0xE6, 0x09, 0xE7,
                                0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0};
    load(7, d, sizeof d);
    reset();
    uint8_t r[] = {0x02}; /* LShift */
    parse_report(7, r, sizeof r);
    CHECK(kb_has(0xE1) && !kb_has(0x00) && !kb_has(0xE0), "listed usages wrong");
}

/* A Report ID used only by a Feature between Push and Pop does not split the
 * Input items around it. */
static void test_feature_id_in_push_pop(void) {
    static const uint8_t d[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x15, 0x81, 0x25, 0x7F,
                                0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xA4, 0x85, 0x02, 0x09, 0x48, 0x15, 0x00, 0x25,
                                0x01, 0x35, 0x01, 0x45, 0x78, 0xB1, 0x02, 0xB4, 0x09, 0x31, 0x81, 0x06, 0xC0};
    load(8, d, sizeof d);
    reset();
    uint8_t r[] = {0x01, 0x05, 0x07};
    parse_report(8, r, sizeof r);
    CHECK(n_mouse == 1 && last_mouse.x == 5 && last_mouse.y == 7, "x=%d y=%d (want 5, 7)", last_mouse.x, last_mouse.y);
}

/* Consumer array with two 16-bit elements: two usages, not one number. */
static void test_consumer_array(void) {
    static const uint8_t d[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x19, 0x00, 0x2A, 0x9C, 0x02, 0x15,
                                0x00, 0x26, 0x9C, 0x02, 0x75, 0x10, 0x95, 0x02, 0x81, 0x00, 0xC0};
    load(5, d, sizeof d);
    reset();
    uint8_t r[] = {0xE9, 0x00, 0xEA, 0x00};
    parse_report(5, r, sizeof r);
    CHECK(n_consumer == 2 && consumer[0] == 0xE9 && consumer[1] == 0xEA, "consumer: %d usages %04X %04X",
          n_consumer, consumer[0], consumer[1]);
}

/* A mouse like QMK's own (Report ID 2 inside the collection, 16-bit X/Y,
 * wheel and pan), as the ELECOM-style composite devices look. */
static void test_mouse_composite(void) {
    static const uint8_t d[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05,
                                0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x95, 0x08, 0x75, 0x01,
                                0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF,
                                0x7F, 0x95, 0x02, 0x75, 0x10, 0x81, 0x06, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F,
                                0x95, 0x01, 0x75, 0x08, 0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81,
                                0x25, 0x7F, 0x95, 0x01, 0x75, 0x08, 0x81, 0x06, 0xC0, 0xC0};
    load(6, d, sizeof d);
    reset();
    uint8_t r[] = {0x02, 0x01, 0x10, 0x00, 0xFE, 0xFF, 0x01, 0xFF};
    parse_report(6, r, sizeof r);
    CHECK(last_mouse.has_button && last_mouse.button == 1 && last_mouse.x == 16 && last_mouse.y == -2 &&
              last_mouse.v == 1 && last_mouse.h == -1,
          "mouse b=%u x=%d y=%d v=%d h=%d", last_mouse.button, last_mouse.x, last_mouse.y, last_mouse.v, last_mouse.h);
}

int main(void) {
    test_report_id_before_collection();
    test_report_id_push_pop();
    test_keyboard_array_offset();
    test_keyboard_boot_layout();
    test_keyboard_listed_usages();
    test_feature_id_in_push_pop();
    test_consumer_array();
    test_mouse_composite();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("parser tests passed\n");
    return 0;
}
