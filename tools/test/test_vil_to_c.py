#!/usr/bin/env python3
"""Tests for tools/vil_to_c.py: keycode names as Vial saves them, and the
macro byte encoding macro.c plays back."""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import vil_to_c as v  # noqa: E402

# A few lines in the form of quantum/vial_ensure_keycode.h.
ENSURE = '''
_Static_assert(KC_NO == 0x00, "");
_Static_assert(KC_TRNS == 0x01, "");
_Static_assert(KC_A == 0x04, "");
_Static_assert(KC_C == 0x06, "");
_Static_assert(KC_N == 0x11, "");
_Static_assert(KC_ENTER == 0x28, "");
_Static_assert(KC_BTN5 == 0xD5, "");
_Static_assert(KC_LCTRL == 0xE0, "");
_Static_assert(QK_KB == 0x7E00, "");
_Static_assert(QK_MACRO == 0x7700, "");
_Static_assert(QK_MOMENTARY == 0x5220, "");
_Static_assert(QK_TO == 0x5200, "");
_Static_assert(QK_TOGGLE_LAYER == 0x5260, "");
_Static_assert(QK_DEF_LAYER == 0x5240, "");
_Static_assert(QK_ONE_SHOT_LAYER == 0x5280, "");
_Static_assert(QK_LAYER_TAP_TOGGLE == 0x52C0, "");
_Static_assert(QK_TAP_DANCE == 0x5700, "");
_Static_assert(QK_LAYER_TAP == 0x4000, "");
_Static_assert(QK_ONE_SHOT_MOD == 0x52A0, "");
_Static_assert(QK_LAYER_MOD == 0x5000, "");
_Static_assert(MOD_LCTL == 0x01, "");
_Static_assert(MOD_LSFT == 0x02, "");
_Static_assert(LCTL(kc) == 0x100, "");
_Static_assert(C_S(kc) == 0x300, "");
'''

failed = 0


def check(cond, what):
    global failed
    if not cond:
        failed += 1
        print('FAIL', what)


with tempfile.NamedTemporaryFile('w', suffix='.h', delete=False) as f:
    f.write(ENSURE)
    path = f.name
try:
    r = v.Resolver(*v.load_names(path))
finally:
    os.unlink(path)

cases = {
    'KC_A': 0x04, 'KC_LCTRL': 0xE0, -1: 0, 'KC_NO': 0, '0x66': 0x66,
    'LCTL(KC_C)': 0x0106, 'C_S(KC_N)': 0x0311, 'LT1(KC_BTN5)': 0x41D5,
    'TD(2)': 0x5702, 'MO(3)': 0x5223, 'USER02': 0x7E02, 'M1': 0x7701,
    'OSM(MOD_LCTL|MOD_LSFT)': 0x52A3, 'LM2(MOD_LSFT)': 0x5042,
}
for name, want in cases.items():
    got = r(name)
    check(got == want, '%r -> 0x%04X, want 0x%04X' % (name, got, want))

try:
    r('KC_NOT_A_KEY')
    check(False, 'unknown name accepted')
except ValueError:
    pass

# tap of a basic key, of a 16-bit key, of a 16-bit key whose low byte is 0,
# a delay of 300 ms, text, then the end of the macro.
got = v.encode_macro([['tap', 'KC_A', 'LCTL(KC_C)', 'LCTL(KC_NO)'], ['delay', 300], ['text', 'hi']], r)
want = bytes([1, 1, 0x04, 1, 5, 0x06, 0x01, 1, 5, 0x01, 0xFF, 1, 4, 300 % 255 + 1, 300 // 255 + 1]) + b'hi\0'
check(got == want, 'macro bytes %s' % got.hex(' '))
got = v.encode_macro([['down', 'KC_ENTER'], ['up', 'KC_ENTER']], r)
check(got == bytes([1, 2, 0x28, 1, 3, 0x28, 0]), 'down/up bytes %s' % got.hex(' '))

if failed:
    print('%d case(s) failed' % failed)
    sys.exit(1)
print('vil_to_c: ok')
