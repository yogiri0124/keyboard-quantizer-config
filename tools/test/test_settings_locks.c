/* Host tests for settings.c, locks.c, and the report sender and injected
 * taps in mouse.c (with the real modes.c and scroll_out.c), against QMK
 * stand-ins. */

#include <string.h>

#include "check.h"
#include "qmk_stub.h"

#include "kq_keycodes.h"
#include "locks.h"
#include "pointing_device.h"
#include "modes.h"
#include "mouse.h"
#include "scroll_out.h"
#include "settings.h"
#include "via.h"

/* ---- QMK stand-ins ---- */

static uint32_t eeprom_user;
static int      eeprom_writes;
static uint16_t now = 1000;
static uint8_t  mods, mousekey_buttons, room = 7;
static int      override_us_jp, override_jp_us, override_removed;
static report_mouse_t sent[8];
static int            n_sent;
keymap_config_t       keymap_config;

uint32_t eeconfig_read_user(void) {
    return eeprom_user;
}
void eeconfig_update_user(uint32_t v) {
    eeprom_user = v;
    eeprom_writes++;
}
uint16_t timer_read(void) {
    return now;
}
uint16_t timer_elapsed(uint16_t last) {
    return (uint16_t)(now - last);
}
void register_mods(uint8_t m) {
    mods |= m;
}
void unregister_mods(uint8_t m) {
    mods &= (uint8_t)~m;
}
uint8_t get_mods(void) {
    return mods;
}
/* Alt and GUI swapped when keymap_config says so, as QMK's Magic swap. */
uint16_t keycode_config(uint16_t kc) {
    if (keymap_config.swap_lalt_lgui) {
        if (kc == KC_LALT) {
            return KC_LGUI;
        }
        if (kc == KC_LGUI) {
            return KC_LALT;
        }
    }
    return kc;
}
void register_us_key_on_jp_os_overrides(void) {
    override_us_jp++;
}
void register_jp_key_on_us_os_overrides(void) {
    override_jp_us++;
}
void remove_all_os_key_overrides(void) {
    override_removed++;
}
void host_mouse_send(report_mouse_t *r) {
    if (n_sent < 8) {
        sent[n_sent] = *r;
    }
    n_sent++;
}
report_mouse_t mousekey_get_report(void) {
    report_mouse_t r = {0};
    r.buttons        = mousekey_buttons;
    return r;
}
uint8_t action_tapping_waiting_room(void) {
    return room;
}
void send_string(const char *s) {
    (void)s;
}
int32_t scroll_out_host_multiplier(bool vertical) {
    (void)vertical;
    return 120;
}
/* Key events QMK was handed: what mouse_tap_cell sends. */
static keyevent_t events[4];
static int        n_events;
static bool       injecting_seen[4];
void              action_exec(keyevent_t e) {
    if (n_events < 4) {
        injecting_seen[n_events] = mouse_injecting();
        events[n_events++]       = e;
    }
}
void wait_ms(uint32_t ms) {
    now = (uint16_t)(now + ms);
}

/* ---- stand-ins for ball.c and wheel.c ---- */

static int ball_resets, setting_changes;
void       ball_init(void) {}
void       ball_reset(void) {
    ball_resets++;
}
char ball_mode_letter(void) {
    return 'c';
}
void ball_setting_changed(setting_t s) {
    (void)s;
    setting_changes++;
}
void wheel_reset(void) {}
void wheel_task(void) {}

/* ---- settings ---- */

static void via(uint8_t command, uint8_t id, uint8_t value, uint8_t *out) {
    uint8_t data[4] = {command, id_custom_channel, id, value};
    settings_via_command(data, sizeof data);
    if (out != NULL) {
        memcpy(out, data, sizeof data);
    }
}

static void test_settings_load(void) {
    /* Fresh EEPROM (all zero): defaults, saved once. */
    eeprom_user = 0;
    eeprom_writes = override_removed = 0;
    settings_load();
    CHECK(settings_get(SETTING_SCROLL_DIV) == 32 && settings_get(SETTING_GESTURE_STEP) == 50 &&
              settings_get(SETTING_CURSOR_SCALE) == 16,
          "defaults");
    CHECK_EQ(eeprom_writes, 1);
    CHECK_EQ(override_removed, 1);
    /* Saved values come back as they are, without a write. */
    eeprom_user   = 0x01U | (40U << 8) | (70U << 16) | (20U << 24);
    eeprom_writes = override_us_jp = 0;
    settings_load();
    CHECK(settings_get(SETTING_SCROLL_DIV) == 40 && settings_get(SETTING_GESTURE_STEP) == 70 &&
              settings_get(SETTING_CURSOR_SCALE) == 20,
          "saved values");
    CHECK(eeprom_writes == 0 && override_us_jp == 1 && os_override_get() == OS_OVERRIDE_US_ON_JP, "override");
    /* The two spare bits above the cursor scale are ignored. */
    eeprom_user = (40U << 8) | (70U << 16) | ((20U | 0xC0U) << 24);
    settings_load();
    CHECK_EQ(settings_get(SETTING_CURSOR_SCALE), 20);
}

static void test_settings_via_and_keys(void) {
    eeprom_user = 0;
    settings_load();
    uint8_t out[4];
    eeprom_writes = 0;
    via(id_custom_set_value, 1, 8, NULL);
    via(id_custom_get_value, 1, 0, out);
    CHECK_EQ(out[3], 8);
    via(id_custom_set_value, 1, 200, NULL); /* clamped */
    CHECK_EQ(settings_get(SETTING_SCROLL_DIV), 80);
    /* Written once, after the slider settles. */
    CHECK_EQ(eeprom_writes, 0);
    now = (uint16_t)(now + 749);
    settings_task();
    CHECK_EQ(eeprom_writes, 0);
    now = (uint16_t)(now + 1);
    settings_task();
    settings_task();
    CHECK_EQ(eeprom_writes, 1);
    CHECK_EQ((eeprom_user >> 8) & 0xFF, 80);
    /* Save right away on request. */
    via(id_custom_set_value, 3, 24, NULL);
    via(id_custom_save, 0, 0, NULL);
    CHECK_EQ(eeprom_writes, 2);
    /* Another channel is not ours. */
    uint8_t other[4] = {id_custom_get_value, 1, 1, 0};
    settings_via_command(other, sizeof other);
    CHECK_EQ(other[0], id_unhandled);

    /* Keys step the values, clamped at the ends. */
    settings_set(SETTING_SCROLL_DIV, 32);
    settings_step(SETTING_SCROLL_DIV, -1);
    CHECK_EQ(settings_get(SETTING_SCROLL_DIV), 28);
    settings_set(SETTING_GESTURE_STEP, 20);
    settings_step(SETTING_GESTURE_STEP, -1);
    CHECK_EQ(settings_get(SETTING_GESTURE_STEP), 20);
    CHECK(settings_set_by_name("threshold", 90) && settings_get(SETTING_GESTURE_STEP) == 90, "alias");
    CHECK(!settings_set_by_name("speed", 1), "unknown name accepted");

    /* The menu's reset: defaults, saved, latches gone. */
    modes_oneshot_key(KQ_MODE_SCROLL, true);
    eeprom_writes = 0;
    via(id_custom_set_value, 4, 1, NULL);
    CHECK(settings_get(SETTING_SCROLL_DIV) == 32 && eeprom_writes == 1, "reset");
    CHECK(!modes_active(KQ_MODE_SCROLL), "reset kept a latch");
}

static void test_os_override(void) {
    eeprom_user = 0;
    settings_load();
    eeprom_writes = override_jp_us = override_removed = 0;
    os_override_set(OS_OVERRIDE_JP_ON_US);
    CHECK(override_jp_us == 1 && eeprom_writes == 1 && (eeprom_user & 3) == OS_OVERRIDE_JP_ON_US, "ju");
    os_override_set(9);
    CHECK(os_override_get() == OS_OVERRIDE_OFF && override_removed == 1, "bad mode not off");
    /* Registered again when the Alt/GUI swap changes. */
    os_override_set(OS_OVERRIDE_US_ON_JP);
    os_override_refresh();
    override_us_jp = 0;
    os_override_refresh();
    CHECK_EQ(override_us_jp, 0);
    keymap_config.swap_lalt_lgui = true;
    os_override_refresh();
    CHECK_EQ(override_us_jp, 1);
    keymap_config.swap_lalt_lgui = false;
}

/* ---- locks ---- */

static void test_locks(void) {
    locks_clear();
    mods = 0;
    CHECK(locks_process_key(U_DLCK, true) && locks_buttons() == MOUSE_BTN1, "drag lock on");
    locks_process_key(U_DLCK, false);
    locks_process_key(U_DLCK, true);
    CHECK_EQ(locks_buttons(), 0);
    locks_process_key(U_DRCK, true);
    locks_button_pressed(KC_MS_BTN2); /* clicking that button ends it */
    CHECK_EQ(locks_buttons(), 0);

    CHECK(locks_process_key(U_LCTL_LOCK, true) && (mods & MOD_BIT(KC_LCTL)), "Ctrl lock on");
    locks_process_key(U_LCTL_LOCK, true);
    CHECK_EQ(mods, 0);

    /* With Ctrl physically held, turning the lock off keeps Ctrl. */
    locks_process_key(U_LCTL_LOCK, true);
    locks_note_key(KC_LCTL, true); /* pressing Ctrl ends the lock... */
    CHECK(!locks_mod(0), "pressing Ctrl kept the lock");
    register_mods(MOD_BIT(KC_LCTL));
    locks_process_key(U_LCTL_LOCK, true);
    locks_process_key(U_LCTL_LOCK, true); /* ...and the lock going off leaves it held */
    CHECK(mods & MOD_BIT(KC_LCTL), "Ctrl dropped while held");
    locks_note_key(KC_LCTL, false);
    unregister_mods(MOD_BIT(KC_LCTL));

    /* Alt/GUI swapped: the Alt key sends GUI, so it ends the GUI lock. */
    keymap_config.swap_lalt_lgui = true;
    locks_process_key(U_LGUI_LOCK, true);
    locks_note_key(KC_LALT, true);
    CHECK(!locks_mod(3), "swapped Alt did not end the GUI lock");
    locks_note_key(KC_LALT, false);
    keymap_config.swap_lalt_lgui = false;

    /* Something dropped a locked modifier: it comes back. */
    mods = 0;
    locks_process_key(U_LSFT_LOCK, true);
    mods = 0;
    locks_task();
    CHECK(mods & MOD_BIT(KC_LSFT), "locked Shift not restored");
    locks_clear();
    CHECK_EQ(mods, 0);
}

/* ---- mouse report ---- */

static void test_mouse_report(void) {
    mouse_reset_latches();
    pointing_device_task();
    n_sent = 0;
    CHECK(!pointing_device_task() && n_sent == 0, "sent with nothing to say");

    mouse_add_motion(40000, -40000);
    pointing_device_task();
    pointing_device_task();
    CHECK(n_sent == 2 && sent[0].x == 32767 && sent[0].y == -32767 && sent[1].x == 7233 && sent[1].y == -7233,
          "motion split: %d reports, %d %d / %d %d", n_sent, sent[0].x, sent[0].y, sent[1].x, sent[1].y);

    /* Buttons: mousekey's and the drag locks', sent on change only. */
    n_sent           = 0;
    mousekey_buttons = MOUSE_BTN1;
    pointing_device_keycode_handler(KC_MS_BTN1, true);
    CHECK(n_sent == 1 && sent[0].buttons == MOUSE_BTN1, "click not sent");
    pointing_device_task();
    CHECK_EQ(n_sent, 1);
    mousekey_buttons = 0;
    pointing_device_keycode_handler(KC_MS_BTN1, false);
    locks_process_key(U_DMID, true);
    pointing_device_task();
    CHECK(n_sent == 3 && sent[2].buttons == MOUSE_BTN3, "drag lock button: %d", sent[2].buttons);

    /* A direct send repeats the buttons the PC has. */
    report_mouse_t wheel = {.v = 5};
    mouse_send_direct(&wheel);
    CHECK(sent[3].buttons == MOUSE_BTN3 && sent[3].v == 5, "direct send");
    mouse_reset_latches();
    pointing_device_task();
    CHECK_EQ(sent[4].buttons, 0);

    /* Scroll output goes out with the report, a report's worth at a time. */
    n_sent = 0;
    scroll_out_detents(2, true, false);
    mouse_request_send();
    pointing_device_task();
    pointing_device_task();
    CHECK(n_sent == 2 && sent[0].v == 127 && sent[1].v == 113, "scroll %d %d", sent[0].v, sent[1].v);

    room = 1;
    CHECK(!mouse_can_tap(), "tap with one free slot");
    room = 2;
    CHECK(mouse_can_tap(), "no tap with two free slots");
}

/* ---- injected taps ---- */

static void test_tap_cell(void) {
    keypos_t pos = {.row = 31, .col = 2};
    uint16_t t0  = now;
    n_events     = 0;
    mouse_tap_cell(pos, 10);
    CHECK(n_events == 2 && events[0].pressed && !events[1].pressed, "tap events: %d", n_events);
    CHECK(events[0].key.row == 31 && events[0].key.col == 2 && events[1].key.col == 2, "tap position");
    CHECK(events[0].type == KEY_EVENT && events[0].time != 0 && events[1].time != 0, "tap event type / time");
    CHECK((uint16_t)(now - t0) == 10 && (uint16_t)(events[1].time - events[0].time) >= 10, "held %u ms",
          (unsigned)(uint16_t)(now - t0));
    CHECK(injecting_seen[0] && injecting_seen[1] && !mouse_injecting(), "inject flag around the tap");
    n_events = 0;
    mouse_tap_cell(pos, 0);
    CHECK(n_events == 2 && now == (uint16_t)(t0 + 10), "a zero hold waited");
}

int main(void) {
    test_settings_load();
    test_settings_via_and_keys();
    test_os_override();
    test_locks();
    test_mouse_report();
    test_tap_cell();
    return check_done("settings / locks / report");
}
