#!/bin/sh
# Builds and runs the host tests: the parts of the firmware that are plain C
# (HID parser, queues, gesture, cursor, scroll, modes) compiled for this
# machine and checked against known cases.
#   tools/test/run.sh            (CC overrides the compiler)
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
kq="$root/keyboards/sekigon/keyboard_quantizer"
vial="$kq/mini/keymaps/vial"
out=${TMPDIR:-/tmp}/kq-host-tests
mkdir -p "$out"
cc=${CC:-cc}
flags="-std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g -I$root/tools/test -I$kq/parser -I$kq/mini -I$vial"

run() {
    name=$1
    shift
    # shellcheck disable=SC2086
    $cc $flags -o "$out/$name" "$@"
    "$out/$name"
}

run parser "$root/tools/test/test_parser.c" "$kq/parser/hid_desc.c" "$kq/parser/hid_report.c"

run queue "$root/tools/test/test_queue.c" "$kq/mini/hid_queue.c"

run input "$root/tools/test/test_input.c" "$kq/mini/hid_input.c" "$kq/mini/hid_queue.c" "$kq/parser/hid_desc.c" "$kq/parser/hid_report.c"

run mouse_core -DMOUSE_WHEEL_RESOLUTION_MULTIPLIER=120 "$root/tools/test/test_mouse_core.c" "$vial/modes.c" "$vial/pointer.c" "$vial/gesture.c" "$vial/scroll_out.c"
