#!/usr/bin/env python3
"""Tests for util/build_id.py: what resets the saved configuration and what
does not."""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'util'))
import build_id as b  # noqa: E402

failed = 0


def check(cond, what):
    global failed
    if not cond:
        failed += 1
        print('FAIL', what)


RULES = 'VIA_ENABLE = yes\nTAP_DANCE_ENABLE = yes\n\n# sources\nSRC += a.c\nVPATH += x\n'
CONFIG = '#pragma once\n#define DYNAMIC_KEYMAP_LAYER_COUNT 8 // layers\n'
MAKE = 'ifeq ($(strip $(VIAL_ENABLE)), yes)\n    OPT_DEFS += -DVIAL_ENABLE\nendif\n'


def build_id(files):
    with tempfile.TemporaryDirectory() as root:
        for path, text in files.items():
            full = os.path.join(root, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'w', newline='') as f:
                f.write(text)
        return b.compute(root)


base = {b.KEYMAP_RULES: RULES, b.KEYBOARD + '/keymaps/vial/config.h': CONFIG, 'builddefs/build_vial.mk': MAKE}
base_id = build_id(base)
check(0 <= base_id < 0xFFFFFF, 'id 0x%X out of range' % base_id)


def same(what, path, text):
    check(build_id(dict(base, **{path: text})) == base_id, what + ' changed the id')


def differs(what, path, text):
    check(build_id(dict(base, **{path: text})) != base_id, what + ' kept the id')


same('CRLF line ends', b.KEYMAP_RULES, RULES.replace('\n', '\r\n'))
same('a make comment', b.KEYMAP_RULES, RULES + '# more\n')
same('a source file', b.KEYMAP_RULES, RULES + 'SRC += b.c\n')
same('a target-specific CFLAGS', b.KEYMAP_RULES, RULES + '$(BUILD_DIR)/x.o:: CFLAGS += -Dy=z\n')
same('a C comment', b.KEYBOARD + '/keymaps/vial/config.h', CONFIG + '/* note */\n')
differs('a feature switch', b.KEYMAP_RULES, RULES.replace('TAP_DANCE_ENABLE = yes', 'TAP_DANCE_ENABLE = no'))
differs('a new feature', b.KEYMAP_RULES, RULES + 'COMBO_ENABLE = yes\n')
differs('a layer count', b.KEYBOARD + '/keymaps/vial/config.h', CONFIG.replace('8', '6'))
differs('a define in QMK make files', 'builddefs/build_vial.mk', MAKE.replace('VIAL_ENABLE\n', 'VIAL_ENABLE -DX\n'))
differs('a missing file', b.KEYBOARD + '/keymaps/vial/config.h', '')

if failed:
    print('%d case(s) failed' % failed)
    sys.exit(1)
print('build_id: ok')
