#!/bin/sh
# Builds and runs the host tests: the firmware's own code compiled for this
# machine (against the QMK stand-ins in stubs/ where it includes QMK) and
# checked against known cases, then the Python tools the build runs
# (descriptor checker, default.vil conversion, build id).
#   tools/test/run.sh            (CC / PYTHON override the compiler / python)
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
kq="$root/keyboards/sekigon/keyboard_quantizer"
vial="$kq/mini/keymaps/vial"
out=${TMPDIR:-/tmp}/kq-host-tests
mkdir -p "$out"
cc=${CC:-cc}
flags="-std=gnu11 -Wall -Wextra -Wshadow -Wswitch-enum -Wpointer-arith -Wundef -Werror -Wno-unused-parameter -g -fsanitize=undefined -fsanitize-undefined-trap-on-error -I$root/tools/test -I$kq/parser -I$kq/mini -I$vial"

run() {
    name=$1
    shift
    # shellcheck disable=SC2086
    $cc $flags -o "$out/$name" "$@"
    "$out/$name"
}

run parser "$root/tools/test/test_parser.c" "$kq/parser/hid_desc.c" "$kq/parser/hid_report.c" "$kq/mini/hid_scan.c"

run queue "$root/tools/test/test_queue.c" "$kq/mini/hid_queue.c" "$kq/mini/hid_scan.c"

run input "$root/tools/test/test_input.c" "$kq/mini/hid_input.c" "$kq/mini/hid_queue.c" "$kq/mini/hid_scan.c" "$kq/parser/hid_desc.c" "$kq/parser/hid_report.c"

run mouse_core -DMOUSE_WHEEL_RESOLUTION_MULTIPLIER=120 "$root/tools/test/test_mouse_core.c" "$vial/modes.c" "$vial/pointer.c" "$vial/gesture.c" "$vial/scroll_out.c"

run macro -I"$root/tools/test/stubs" -DQMK_KEYBOARD_H='"qmk_stub.h"' "$root/tools/test/test_macro.c" "$vial/macro.c"

run mouse_keys -I"$root/tools/test/stubs" -DQMK_KEYBOARD_H='"qmk_stub.h"' -DVIAL_ENABLE -DTAP_DANCE_ENABLE "$root/tools/test/test_mouse_keys.c" "$vial/mouse_keys.c" "$vial/modes.c"

run settings_locks -I"$root/tools/test/stubs" -DQMK_KEYBOARD_H='"qmk_stub.h"' -DMOUSEKEY_ENABLE -DMOUSE_WHEEL_RESOLUTION_MULTIPLIER=120 "$root/tools/test/test_settings_locks.c" "$vial/settings.c" "$vial/locks.c" "$vial/mouse.c" "$vial/modes.c" "$vial/scroll_out.c"

run ball_wheel -I"$root/tools/test/stubs" -DQMK_KEYBOARD_H='"qmk_stub.h"' -DMOUSEKEY_ENABLE -DMOUSE_WHEEL_RESOLUTION_MULTIPLIER=120 "$root/tools/test/test_ball_wheel.c" "$vial/ball.c" "$vial/wheel.c" "$vial/gesture.c" "$vial/pointer.c" "$vial/scroll_out.c" "$vial/modes.c"

${PYTHON:-python3} -B "$root/tools/test/test_hid_descriptors.py"
${PYTHON:-python3} -B "$root/tools/test/test_vil_to_c.py"
${PYTHON:-python3} -B "$root/tools/test/test_build_id.py"
