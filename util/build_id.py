"""Build id for Vial's EEPROM check (replaces vial-qmk's util/build_id.py).

vial-qmk picks a random id per build and resets the saved configuration
(keymap, tap dances, macros, settings) whenever the id changes, so every new
firmware came up empty. Here the id is derived from what decides the EEPROM
layout instead: the saved configuration survives a reflash, and is reset only
when its layout may have changed (then default.vil is written in again, see
keymaps/vial/default_config.c).

Run by builddefs/build_keyboard.mk from the QMK root.
"""

import hashlib
import os

LAYOUT_FILES = [
    'quantum/eeconfig.h',
    'quantum/via.h',
    'quantum/vial.h',
    'quantum/dynamic_keymap.c',
    'quantum/qmk_settings.h',
    'keyboards/sekigon/keyboard_quantizer/mini/info.json',
    'keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial/config.h',
]


def main():
    h = hashlib.sha256()
    for path in LAYOUT_FILES:
        h.update(path.encode())
        if os.path.exists(path):
            with open(path, 'rb') as f:
                h.update(f.read().replace(b'\r\n', b'\n'))
    build_id = int.from_bytes(h.digest()[:3], 'little')
    if build_id == 0xFFFFFF:  # what an invalidated EEPROM holds
        build_id = 0xFFFFFE
    print("#define BUILD_ID ((uint32_t)0x{:08X})".format(build_id))


if __name__ == "__main__":
    main()
