/* Host tests for the plain-C mouse cores: modes, pointer, gesture, scroll. */

#include <string.h>

#include "check.h"
#include "gesture.h"
#include "modes.h"
#include "pointer.h"
#include "scroll_out.h"

static int32_t host_multiplier = 1;
int32_t        scroll_out_host_multiplier(bool vertical) {
    (void)vertical;
    return host_multiplier;
}

/* ---- modes ---- */

static void test_modes(void) {
    modes_reset();
    /* Physical holds are counted. */
    modes_hold_key(KQ_MODE_SCROLL, true, false);
    modes_hold_key(KQ_MODE_SCROLL, true, false);
    modes_hold_key(KQ_MODE_SCROLL, false, false);
    CHECK(modes_active(KQ_MODE_SCROLL), "second holder dropped");
    modes_hold_key(KQ_MODE_SCROLL, false, false);
    CHECK(!modes_active(KQ_MODE_SCROLL), "still on");

    /* Magic TAP of a hold key (press and release in one pass) arms the
     * oneshot; a HOLD (release in a later pass) does not. */
    modes_new_pass();
    modes_hold_key(KQ_MODE_GESTURE, true, true);
    modes_hold_key(KQ_MODE_GESTURE, false, true);
    CHECK(modes_active(KQ_MODE_GESTURE), "TAP did not arm the oneshot");
    modes_end_oneshots();
    CHECK(!modes_active(KQ_MODE_GESTURE), "oneshot survived the next key");
    modes_hold_key(KQ_MODE_GESTURE, true, true);
    modes_new_pass();
    CHECK(modes_active(KQ_MODE_GESTURE), "HOLD not active");
    modes_hold_key(KQ_MODE_GESTURE, false, true);
    CHECK(!modes_active(KQ_MODE_GESTURE), "HOLD left the mode on");

    /* Oneshots are exclusive and the key toggles its own. */
    modes_oneshot_key(KQ_MODE_SCROLL, true);
    modes_oneshot_key(KQ_MODE_SLOW, true);
    CHECK(!modes_active(KQ_MODE_SCROLL) && modes_active(KQ_MODE_SLOW), "oneshots not exclusive");
    modes_oneshot_key(KQ_MODE_SLOW, true);
    CHECK(!modes_active(KQ_MODE_SLOW), "second press did not disarm");

    /* Toggle: a physical one lasts; a magic TAP one ends with the next key;
     * a magic HOLD one lasts. */
    modes_toggle_key(KQ_MODE_SCROLL, true, false);
    modes_toggle_key(KQ_MODE_SCROLL, false, false);
    modes_end_magic_latches();
    CHECK(modes_active(KQ_MODE_SCROLL), "physical toggle ended");
    modes_toggle_key(KQ_MODE_SCROLL, true, false);
    CHECK(!modes_active(KQ_MODE_SCROLL), "toggle off");
    modes_new_pass();
    modes_toggle_key(KQ_MODE_SCROLL, true, true);
    modes_toggle_key(KQ_MODE_SCROLL, false, true);
    CHECK(modes_active(KQ_MODE_SCROLL), "magic toggle off");
    modes_end_magic_latches();
    CHECK(!modes_active(KQ_MODE_SCROLL), "magic TAP toggle survived");
    modes_toggle_key(KQ_MODE_SCROLL, true, true);
    modes_new_pass();
    modes_toggle_key(KQ_MODE_SCROLL, false, true);
    modes_end_magic_latches();
    CHECK(modes_active(KQ_MODE_SCROLL), "magic HOLD toggle ended");

    /* S.SCR: held, or latched by a TAP until the next key. */
    modes_reset();
    modes_new_pass();
    modes_slow_scroll_key(true, true);
    modes_slow_scroll_key(false, true);
    CHECK(modes_slow_scroll_active(), "TAP latch");
    modes_slow_scroll_key(true, false);
    modes_end_magic_latches();
    CHECK(modes_slow_scroll_active(), "physical hold dropped with the latch");
    modes_slow_scroll_key(false, false);
    CHECK(!modes_slow_scroll_active(), "still on");
}

/* ---- pointer ---- */

static void test_pointer(void) {
    pointer_t p;
    pointer_reset(&p);
    int32_t x, y;
    /* 1:1 */
    pointer_move(&p, 5, -3, 16, false, &x, &y);
    CHECK(x == 5 && y == -3, "1:1 got %ld %ld", (long)x, (long)y);
    /* Half speed: remainders carry, nothing is lost over many reports. */
    int32_t sx = 0, sy = 0;
    for (int i = 0; i < 100; i++) {
        pointer_move(&p, 1, -1, 8, false, &x, &y);
        sx += x;
        sy += y;
    }
    CHECK(sx == 50 && sy == -50, "half speed lost counts: %ld %ld", (long)sx, (long)sy);
    /* Slow cursor divides by 4 more. */
    pointer_reset(&p);
    sx = 0;
    for (int i = 0; i < 64; i++) {
        pointer_move(&p, 1, 0, 16, true, &x, &y);
        sx += x;
    }
    CHECK_EQ(sx, 16);
    /* Recoil: a small reverse tick right after a fast move is dropped. */
    pointer_reset(&p);
    pointer_move(&p, 80, 0, 16, false, &x, &y);
    pointer_move(&p, -3, 0, 16, false, &x, &y);
    CHECK_EQ(x, 0);
    pointer_move(&p, -3, 0, 16, false, &x, &y);
    CHECK_EQ(x, -3);
    /* A wheel-only report (no motion) does not reset the filter. */
    pointer_move(&p, 90, 0, 16, false, &x, &y);
    pointer_move(&p, 0, 0, 16, false, &x, &y);
    pointer_move(&p, -2, 0, 16, false, &x, &y);
    CHECK_EQ(x, 0);
}

/* ---- gesture ---- */

static gesture_cfg_t cfg4 = {50, 0};
static gesture_cfg_t cfg8 = {50, 0x0F};

static int taps_of(gesture_t *g, const gesture_cfg_t *c, int16_t dx, int16_t dy, int reports, uint16_t *now,
                   uint16_t dt, gesture_dir_t *dir) {
    int n = 0;
    for (int i = 0; i < reports; i++) {
        gesture_dir_t d = gesture_move(g, c, dx, dy, *now);
        if (d != GESTURE_NONE) {
            n++;
            *dir = d;
        }
        *now = (uint16_t)(*now + dt);
    }
    return n;
}

static void test_gesture_classify(void) {
    CHECK_EQ(gesture_classify(10, 0, 0, false), GESTURE_RIGHT);
    CHECK_EQ(gesture_classify(-10, 0, 0, false), GESTURE_LEFT);
    CHECK_EQ(gesture_classify(0, -10, 0, false), GESTURE_UP);
    CHECK_EQ(gesture_classify(0, 10, 0, false), GESTURE_DOWN);
    /* Exact diagonal: with diagonals it is one, without it is ambiguous. */
    CHECK_EQ(gesture_classify(10, -10, 0x0F, true), GESTURE_UP_RIGHT);
    CHECK_EQ(gesture_classify(10, -10, 0, true), GESTURE_NONE);
    CHECK_EQ(gesture_classify(-10, 10, 0x0F, false), GESTURE_DOWN_LEFT);
    CHECK_EQ(gesture_classify(10, 10, 0x0F, false), GESTURE_DOWN_RIGHT);
    CHECK_EQ(gesture_classify(-10, -10, 0x0F, false), GESTURE_UP_LEFT);
    /* Only the up-right diagonal mapped: up-left falls back to a cardinal. */
    gesture_dir_t ul = gesture_classify(-10, -11, 0x01, false);
    CHECK(ul == GESTURE_UP || ul == GESTURE_LEFT, "unmapped diagonal gave %d", ul);
    /* 30 degrees above right: right with four, up-right with eight. */
    CHECK_EQ(gesture_classify(87, -50, 0, true), GESTURE_RIGHT);
    CHECK_EQ(gesture_classify(87, -50, 0x0F, true), GESTURE_UP_RIGHT);
    /* 22 degrees: right and up-right meet at 22.5, too close to tell. */
    CHECK_EQ(gesture_classify(100, -40, 0x0F, true), GESTURE_NONE);
}

static void test_gesture_strokes(void) {
    gesture_t     g;
    gesture_dir_t dir = GESTURE_NONE;
    uint16_t      now = 1000;
    gesture_reset(&g);
    /* Short movement: nothing. */
    CHECK_EQ(taps_of(&g, &cfg4, 3, 0, 10, &now, 8, &dir), 0);
    /* Reaching one step taps once, right. */
    CHECK_EQ(taps_of(&g, &cfg4, 4, 0, 6, &now, 8, &dir), 1);
    CHECK_EQ(dir, GESTURE_RIGHT);
    /* Each further step taps again (slow: interval never limits). */
    CHECK_EQ(taps_of(&g, &cfg4, 5, 0, 30, &now, 8, &dir), 3);
    /* Locked: drifting up while moving on does not change direction. */
    CHECK_EQ(taps_of(&g, &cfg4, 3, -4, 40, &now, 8, &dir), 2);
    CHECK_EQ(dir, GESTURE_RIGHT);
    /* Moving back does nothing. */
    CHECK_EQ(taps_of(&g, &cfg4, -10, 0, 20, &now, 8, &dir), 0);
    /* Rest ends the stroke; the next one decides afresh. */
    now = (uint16_t)(now + GESTURE_IDLE_MS + 10);
    CHECK_EQ(gesture_poll(&g, now), GESTURE_NONE);
    CHECK(!g.active, "stroke did not end");
    CHECK_EQ(taps_of(&g, &cfg4, 0, -10, 5, &now, 8, &dir), 1);
    CHECK_EQ(dir, GESTURE_UP);
}

static void test_gesture_fast(void) {
    gesture_t     g;
    gesture_dir_t dir = GESTURE_NONE;
    uint16_t      now = 0;
    gesture_reset(&g);
    /* A hard flick: 20 steps in 10 ms. One tap now, at most the backlog
     * later, spaced by the interval. */
    int n = taps_of(&g, &cfg8, -100, 0, 10, &now, 1, &dir);
    CHECK_EQ(n, 1);
    CHECK_EQ(dir, GESTURE_LEFT);
    int later = 0;
    for (int t = 0; t < 400; t++) {
        now++;
        if (gesture_poll(&g, now) != GESTURE_NONE) {
            later++;
        }
    }
    CHECK_EQ(later, GESTURE_BACKLOG_MAX);
    CHECK(!g.active, "stroke did not end after the backlog");
}

static void test_gesture_diagonal(void) {
    gesture_t     g;
    gesture_dir_t dir = GESTURE_NONE;
    uint16_t      now = 0;
    gesture_reset(&g);
    CHECK_EQ(taps_of(&g, &cfg8, 5, 5, 10, &now, 8, &dir), 1);
    CHECK_EQ(dir, GESTURE_DOWN_RIGHT);
    /* Along the diagonal, a step is measured along it: 10 reports of 7.07
     * counts, plus what was left over past the first step, make one more. */
    CHECK_EQ(taps_of(&g, &cfg8, 5, 5, 10, &now, 8, &dir), 1);
    CHECK_EQ(dir, GESTURE_DOWN_RIGHT);
    /* Without diagonals the same movement is ambiguous until two steps, then
     * picks a cardinal. */
    gesture_reset(&g);
    now = 0;
    CHECK_EQ(taps_of(&g, &cfg4, 5, 5, 9, &now, 8, &dir), 0);
    CHECK_EQ(taps_of(&g, &cfg4, 5, 5, 6, &now, 8, &dir), 1);
    CHECK(dir == GESTURE_RIGHT || dir == GESTURE_DOWN, "picked %d", dir);
}

/* ---- scroll output ---- */

static void take_all(int32_t *v, int32_t *h) {
    int8_t tv, th;
    *v = *h = 0;
    for (int i = 0; i < 1000 && scroll_out_pending(); i++) {
        scroll_out_take(&tv, &th);
        *v += tv;
        *h += th;
    }
}

static void test_scroll_out(void) {
    int32_t v, h;
    scroll_out_reset();
    host_multiplier = 1;
    scroll_out_detents(3, true, false);
    take_all(&v, &h);
    CHECK(v == 3 && h == 0, "x1 detents %ld", (long)v);
    host_multiplier = 120;
    scroll_out_detents(-2, false, false);
    take_all(&v, &h);
    CHECK(v == 0 && h == -240, "x120 detents %ld", (long)h);
    /* Ball: 32 counts per detent, 1 count at a time, nothing lost. */
    for (int i = 0; i < 64; i++) {
        scroll_out_ball(0, 1, 32);
    }
    take_all(&v, &h);
    CHECK_EQ(v, 240);
    /* Urgent goes first and alone. */
    scroll_out_detents(1, true, false);
    scroll_out_detents(1, false, true);
    int8_t tv, th;
    CHECK(scroll_out_urgent_pending(), "urgent missing");
    scroll_out_take_urgent(&tv, &th);
    CHECK(tv == 0 && th == 120, "urgent %d %d", tv, th);
    take_all(&v, &h);
    CHECK(v == 120 && h == 0, "rest %ld %ld", (long)v, (long)h);
    /* More than one report's worth splits over reports. */
    scroll_out_detents(5, true, false);
    scroll_out_take(&tv, &th);
    CHECK_EQ(tv, 127);
    take_all(&v, &h);
    CHECK_EQ(v, 600 - 127);
}

int main(void) {
    test_modes();
    test_pointer();
    test_gesture_classify();
    test_gesture_strokes();
    test_gesture_fast();
    test_gesture_diagonal();
    test_scroll_out();
    return check_done("mouse core");
}
