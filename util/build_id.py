"""Build id for Vial's EEPROM check (replaces vial-qmk's util/build_id.py).

vial-qmk picks a random id per build and resets the saved configuration
(keymap, tap dances, macros, settings) whenever the id changes, so every new
firmware came up empty. Here the id comes from the sources that decide where
and how that configuration is stored: the saved configuration survives a
reflash, and is reset when any of them changes (then default.vil is written
in again, see keymaps/vial/default_config.c). Comments and blank space are
left out, so editing a comment does not reset anything.

Run by builddefs/build_keyboard.mk from the QMK root.
"""

import hashlib
import json
import os
import re

KEYBOARD = 'keyboards/sekigon/keyboard_quantizer/mini'

LAYOUT_FILES = [
    # Where QMK, VIA and Vial put things, and how big they are.
    'quantum/eeconfig.h',
    'quantum/eeconfig.c',
    'quantum/via.h',
    'quantum/via.c',
    'quantum/vial.h',
    'quantum/vial.c',
    'quantum/dynamic_keymap.h',
    'quantum/dynamic_keymap.c',
    'quantum/qmk_settings.h',
    'quantum/qmk_settings.c',
    'quantum/keycodes.h',
    'builddefs/build_vial.mk',
    'builddefs/common_features.mk',
    # The settings this keyboard chooses.
    KEYBOARD + '/info.json',
    KEYBOARD + '/config.h',
    KEYBOARD + '/rules.mk',
    KEYBOARD + '/post_rules.mk',
    KEYBOARD + '/keymaps/vial/config.h',
    KEYBOARD + '/keymaps/vial/rules.mk',
    KEYBOARD + '/keymaps/vial/vial.json',
]

COMMENT = re.compile(
    rb'/\*.*?\*/'  # C block comment
    rb'|//[^\n]*'  # C line comment
    rb'|^[ \t]*#(?![ \t]*(?:define|undef|if|ifdef|ifndef|elif|else|endif|include|error)\b)[^\n]*',  # make comment
    re.S | re.M,
)


def significant(path, data):
    data = data.replace(b'\r\n', b'\n')
    if path.endswith('.json'):
        v = json.loads(data)
        if path.endswith('vial.json'):
            # Labels and the drawn layout do not change the storage.
            v = {k: v.get(k) for k in ('matrix', 'tap_dance_entries', 'combo_entries', 'key_override_entries')}
        return json.dumps(v, sort_keys=True).encode()
    data = COMMENT.sub(b'', data)
    return b'\n'.join(line.strip() for line in data.split(b'\n') if line.strip())


def main():
    h = hashlib.sha256()
    for path in LAYOUT_FILES:
        h.update(path.encode() + b'\0')
        if os.path.exists(path):
            with open(path, 'rb') as f:
                h.update(significant(path, f.read()))
    build_id = int.from_bytes(h.digest()[:3], 'little')
    if build_id == 0xFFFFFF:  # what an invalidated EEPROM holds
        build_id = 0xFFFFFE
    print("#define BUILD_ID ((uint32_t)0x{:08X})".format(build_id))


if __name__ == "__main__":
    main()
