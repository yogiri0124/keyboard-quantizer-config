/* Host tests for the HID descriptor compiler and report decoder. */

#include <string.h>

#include "check.h"
#include "hid_desc.h"
#include "hid_report.h"

static void load(uint8_t interface, const uint8_t *d, size_t n) {
    hid_desc_clear();
    CHECK(hid_desc_compile(interface, d, (uint16_t)n), "descriptor for interface %u rejected", interface);
}

static bool decode(uint8_t interface, const uint8_t *r, size_t n, hid_decoded_t *out) {
    return hid_report_decode(interface, r, (uint8_t)n, out);
}

static bool has_key(const hid_decoded_t *d, uint8_t usage) {
    return (d->keys.bits[usage >> 3] >> (usage & 7)) & 1;
}

static int key_count(const hid_decoded_t *d) {
    int n = 0;
    for (int u = 0; u < 256; u++) {
        n += has_key(d, (uint8_t)u);
    }
    return n;
}

/* ELECOM HUGE PLUS mouse interface, rebuilt from its "hid desc" dump:
 * 5 buttons + 3 "constant" bits that carry buttons 6-8, 16-bit X/Y, wheel,
 * AC Pan; consumer, system and two vendor reports on the same interface. */
static const uint8_t huge_mouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,             //
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x05, //
    0x81, 0x02, 0x75, 0x03, 0x95, 0x01, 0x81, 0x01,                                     //
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, //
    0x95, 0x02, 0x81, 0x06, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, //
    0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,       //
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x19, 0x01, 0x2A, 0x8C, 0x02, 0x15, //
    0x01, 0x26, 0x8C, 0x02, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,                   //
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, 0x03, 0x09, 0x82, 0x09, 0x81, 0x09, 0x83, //
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01, //
    0x81, 0x01, 0xC0,                                                                   //
    0x06, 0x00, 0xFF, 0x09, 0x00, 0xA1, 0x01, 0x85, 0x05, 0x15, 0x00, 0x26, 0xFF, 0x00, //
    0x75, 0x08, 0x95, 0x07, 0x81, 0x02, 0xC0,                                           //
};

static void test_huge_plus(void) {
    load(17, huge_mouse, sizeof huge_mouse);
    hid_decoded_t d;
    /* all 8 buttons, x=+16, y=-2, wheel=+1, pan=-1 */
    uint8_t r1[] = {0x01, 0xFF, 0x10, 0x00, 0xFE, 0xFF, 0x01, 0xFF};
    CHECK(decode(17, r1, sizeof r1, &d), "mouse report rejected");
    CHECK_EQ(d.kind, HID_KIND_MOUSE);
    CHECK(d.mouse.has_buttons, "no buttons");
    CHECK_EQ(d.mouse.buttons, 0xFF);
    CHECK_EQ(d.mouse.x, 16);
    CHECK_EQ(d.mouse.y, -2);
    CHECK_EQ(d.mouse.wheel, 1);
    CHECK_EQ(d.mouse.pan, -1);
    CHECK_EQ(d.mouse.undefined, 0);

    uint8_t r2[] = {0x02, 0xE9, 0x00};
    CHECK(decode(17, r2, sizeof r2, &d), "consumer rejected");
    CHECK_EQ(d.kind, HID_KIND_CONSUMER);
    CHECK_EQ(d.usages.count, 1);
    CHECK_EQ(d.usages.usages[0], 0xE9);
    uint8_t r2up[] = {0x02, 0x00, 0x00};
    CHECK(decode(17, r2up, sizeof r2up, &d), "consumer release rejected");
    CHECK_EQ(d.usages.count, 0);

    uint8_t r3[] = {0x03, 0x02}; /* second listed usage: Power Down 0x81 */
    CHECK(decode(17, r3, sizeof r3, &d), "system rejected");
    CHECK_EQ(d.kind, HID_KIND_SYSTEM);
    CHECK_EQ(d.usages.count, 1);
    CHECK_EQ(d.usages.usages[0], 0x81);

    uint8_t r5[] = {0x05, 1, 2, 3, 4, 5, 6, 7};
    CHECK(decode(17, r5, sizeof r5, &d), "vendor rejected");
    CHECK_EQ(d.kind, HID_KIND_OTHER);
    CHECK_EQ(d.collection_page, 0xFF00);
    CHECK_EQ(d.payload_len, 7);
    CHECK_EQ(d.payload[0], 1);

    uint8_t r9[] = {0x09, 0};
    CHECK(!decode(17, r9, sizeof r9, &d), "unknown report ID accepted");
    CHECK(hid_desc_has_kind(17, HID_KIND_MOUSE), "not a mouse");
    CHECK(!hid_desc_has_kind(17, HID_KIND_KEYBOARD), "a keyboard");
}

static const uint8_t boot_keyboard[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, //
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x19, 0x00, 0x29, 0xFF, //
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x00, 0xC0,
};

static void test_boot_keyboard(void) {
    load(16, boot_keyboard, sizeof boot_keyboard);
    hid_decoded_t d;
    uint8_t       r[] = {0x02, 0x00, 0x04, 0x28, 0, 0, 0, 0}; /* LShift, A, Enter */
    CHECK(decode(16, r, sizeof r, &d), "rejected");
    CHECK_EQ(d.kind, HID_KIND_KEYBOARD);
    CHECK(has_key(&d, 0xE1) && has_key(&d, 0x04) && has_key(&d, 0x28), "keys missing");
    CHECK_EQ(key_count(&d), 3);
    /* Rollover error: no new state. */
    uint8_t ro[] = {0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
    CHECK(!decode(16, ro, sizeof ro, &d), "rollover error taken as keys");
}

/* 0..255 written 15 00 25 FF: FF is 255, not -1. */
static void test_unsigned_logical_max(void) {
    static const uint8_t k[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0x00, 0x29, 0xFF,
                                0x15, 0x00, 0x25, 0xFF, 0x95, 0x01, 0x75, 0x08, 0x81, 0x00, 0xC0};
    load(1, k, sizeof k);
    hid_decoded_t d;
    uint8_t       r[] = {0x04};
    CHECK(decode(1, r, sizeof r, &d) && has_key(&d, 0x04), "0..255 array dropped A");
}

/* Array value = index from Logical Minimum into the usage range. */
static void test_array_offset(void) {
    static const uint8_t k[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0x04, 0x29, 0x1D,
                                0x15, 0x04, 0x25, 0x1D, 0x75, 0x08, 0x95, 0x02, 0x81, 0x00, 0xC0};
    load(1, k, sizeof k);
    hid_decoded_t d;
    uint8_t       r[] = {0x04, 0x05};
    CHECK(decode(1, r, sizeof r, &d), "rejected");
    CHECK(has_key(&d, 0x04) && has_key(&d, 0x05) && key_count(&d) == 2, "offset wrong");
    uint8_t none[] = {0x00, 0x00}; /* below Logical Minimum: the null state */
    CHECK(decode(1, none, sizeof none, &d) && key_count(&d) == 0, "null state gave a key");
}

/* Modifiers listed one Usage each, out of order. */
static void test_listed_bitmap(void) {
    static const uint8_t k[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x09, 0xE1, 0x09, 0xE0,
                                0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0xC0};
    load(1, k, sizeof k);
    hid_decoded_t d;
    uint8_t       r[] = {0x02}; /* second bit = listed E0 */
    CHECK(decode(1, r, sizeof r, &d), "rejected");
    CHECK(has_key(&d, 0xE0) && key_count(&d) == 1, "listed usage wrong");
}

/* Report ID before the Collection, and a Push/Pop that restores it. */
static void test_report_ids(void) {
    static const uint8_t m[] = {
        0x05, 0x01, 0x09, 0x02, 0x85, 0x01, 0xA1, 0x01, 0x09, 0x30, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01,
        0x81, 0x06, 0xA4, 0x85, 0x02, 0x09, 0x31, 0x81, 0x06, 0xB4, 0x09, 0x38, 0x81, 0x06, 0xC0,
    };
    load(2, m, sizeof m);
    hid_decoded_t d;
    uint8_t       r1[] = {0x01, 0x05, 0x03}; /* x=5, wheel=3 (after the Pop, back in ID 1) */
    CHECK(decode(2, r1, sizeof r1, &d), "ID 1 rejected");
    CHECK_EQ(d.mouse.x, 5);
    CHECK_EQ(d.mouse.wheel, 3);
    CHECK(!d.mouse.has_buttons, "buttons in a button-less report");
    uint8_t r2[] = {0x02, 0x09};
    CHECK(decode(2, r2, sizeof r2, &d), "ID 2 rejected");
    CHECK_EQ(d.mouse.y, 9);
    CHECK_EQ(d.mouse.x, 0);
}

/* A Report ID that only a Feature uses does not split the Inputs around it. */
static void test_feature_only_id(void) {
    static const uint8_t m[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x15, 0x81, 0x25, 0x7F,
                                0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xA4, 0x85, 0x02, 0x09, 0x48, 0x15, 0x00, 0x25,
                                0x01, 0xB1, 0x02, 0xB4, 0x09, 0x31, 0x81, 0x06, 0xC0};
    load(3, m, sizeof m);
    hid_decoded_t d;
    uint8_t       r[] = {0x01, 0x05, 0x07};
    CHECK(decode(3, r, sizeof r, &d), "rejected");
    CHECK_EQ(d.mouse.x, 5);
    CHECK_EQ(d.mouse.y, 7);
}

/* Consumer arrays: two 16-bit elements; listed usages; one element with a
 * null state. */
static void test_consumer_arrays(void) {
    static const uint8_t two[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x19, 0x00, 0x2A, 0x9C, 0x02, 0x15,
                                  0x00, 0x26, 0x9C, 0x02, 0x75, 0x10, 0x95, 0x02, 0x81, 0x00, 0xC0};
    load(4, two, sizeof two);
    hid_decoded_t d;
    uint8_t       r[] = {0xE9, 0x00, 0xEA, 0x00};
    CHECK(decode(4, r, sizeof r, &d), "rejected");
    CHECK(d.usages.count == 2 && d.usages.usages[0] == 0xE9 && d.usages.usages[1] == 0xEA, "two elements wrong");

    static const uint8_t listed[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x09, 0xE9, 0x09, 0xEA, 0x15, 0x01,
                                     0x25, 0x02, 0x75, 0x08, 0x95, 0x01, 0x81, 0x40, 0xC0};
    load(4, listed, sizeof listed);
    uint8_t one[] = {0x02}, zero[] = {0x00};
    CHECK(decode(4, one, 1, &d) && d.usages.count == 1 && d.usages.usages[0] == 0xEA, "listed array wrong");
    CHECK(decode(4, zero, 1, &d) && d.usages.count == 0, "null state gave a usage");

    static const uint8_t ranged[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x19, 0xB5, 0x29, 0xB6, 0x15, 0x01,
                                     0x25, 0x02, 0x75, 0x08, 0x95, 0x01, 0x81, 0x40, 0xC0};
    load(4, ranged, sizeof ranged);
    CHECK(decode(4, zero, 1, &d) && d.usages.count == 0, "release gave a usage");
    uint8_t first[] = {0x01};
    CHECK(decode(4, first, 1, &d) && d.usages.count == 1 && d.usages.usages[0] == 0xB5, "press wrong");

    /* Bitmap of consumer keys. */
    static const uint8_t bitmap[] = {0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x09, 0xE9, 0x09, 0xEA, 0x09, 0xE2, 0x15,
                                     0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x01, 0xC0};
    load(4, bitmap, sizeof bitmap);
    uint8_t mute[] = {0x04};
    CHECK(decode(4, mute, 1, &d) && d.usages.count == 1 && d.usages.usages[0] == 0xE2, "bitmap wrong");
}

/* Usage Page written after the usages still applies to them; a 4-byte usage
 * carries its own page. */
static void test_usage_pages(void) {
    static const uint8_t m[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x30, 0x05, 0x01, 0x15, 0x81, 0x25,
                                0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0x05, 0x09, 0x0B, 0x38, 0x00, 0x01,
                                0x00, 0x81, 0x06, 0xC0};
    load(5, m, sizeof m);
    hid_decoded_t d;
    uint8_t       r[] = {0x03, 0xFF};
    CHECK(decode(5, r, sizeof r, &d), "rejected");
    CHECK_EQ(d.mouse.x, 3);
    CHECK_EQ(d.mouse.wheel, -1);
    CHECK(!d.mouse.has_buttons, "extended Generic Desktop usage taken as a button");
}

/* Kensington SlimBlade style vendor buttons inside the mouse collection. */
static void test_slimblade(void) {
    static const uint8_t m[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x02, 0x15, 0x00,
                                0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x06, 0x00, 0xFF, 0x75, 0x01, 0x95,
                                0x02, 0x81, 0x02, 0x75, 0x04, 0x95, 0x01, 0x81, 0x01, 0xC0};
    load(6, m, sizeof m);
    hid_decoded_t d;
    uint8_t       r[] = {0x0D}; /* button 1, vendor bits 0b11 */
    CHECK(decode(6, r, sizeof r, &d), "rejected");
    CHECK_EQ(d.mouse.buttons, 0x01 | (0x03 << 4));
}

/* Broken or odd descriptors must neither crash nor half-load. */
static void test_robustness(void) {
    hid_desc_clear();
    static const uint8_t truncated[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x30, 0x16, 0x01};
    CHECK(!hid_desc_compile(7, truncated, sizeof truncated), "truncated descriptor accepted");
    CHECK(hid_desc_device(7) == NULL, "device left behind");
    static const uint8_t long_item[] = {0xFE, 0x02, 0x10, 0xAA, 0xBB, 0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x30,
                                        0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0};
    CHECK(hid_desc_compile(7, long_item, sizeof long_item), "long item broke the parse");
    uint8_t       r[] = {0x02};
    hid_decoded_t d;
    CHECK(decode(7, r, 1, &d) && d.mouse.x == 2, "after long item");
    CHECK(!hid_desc_compile(8, NULL, 10), "NULL descriptor accepted");
    /* Short report: missing bytes read as zero. */
    load(17, huge_mouse, sizeof huge_mouse);
    uint8_t shortr[] = {0x01, 0x01};
    CHECK(decode(17, shortr, sizeof shortr, &d) && d.mouse.buttons == 1 && d.mouse.x == 0, "short report");
    /* Replacing an interface's descriptor frees its old slot. */
    for (int i = 0; i < 20; i++) {
        CHECK(hid_desc_compile(9, boot_keyboard, sizeof boot_keyboard), "recompile %d failed", i);
    }
}

int main(void) {
    test_huge_plus();
    test_boot_keyboard();
    test_unsigned_logical_max();
    test_array_offset();
    test_listed_bitmap();
    test_report_ids();
    test_feature_only_id();
    test_consumer_arrays();
    test_usage_pages();
    test_slimblade();
    test_robustness();
    return check_done("parser");
}
