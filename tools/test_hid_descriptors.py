#!/usr/bin/env python3
"""Regression cases for hid_descriptors.py: each must pass or fail as marked."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hid_descriptors as h  # noqa: E402


def H(text):
    return bytes.fromhex(text.replace(" ", ""))


# A mouse with a Resolution Multiplier feature, the shape Step 3 adds.
RESMUL = H(
    "05 01 09 02 a1 01 85 02"
    "09 01 a1 00"
    "05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02"
    "95 02 75 08 81 01"
    "05 01 09 30 09 31 16 01 80 26 ff 7f 95 02 75 10 81 06"
    "a1 02"  # logical collection: wheel + its multiplier
    "09 48 15 00 25 01 35 00 45 0f 95 01 75 02 b1 02"  # feature, 2 bits
    "a4"  # push
    "09 38 15 81 25 7f 35 00 45 00 75 08 95 01 81 06"
    "b4"  # pop
    "75 06 b1 01"  # 6-bit feature padding
    "c0"
    "c0 c0"
)

# (name, descriptor, expect an error)
CASES = [
    ("0..255 written as 15 00 25 FF", H("05 01 09 06 a1 01 05 07 19 00 29 ff 15 00 25 ff 95 01 75 08 81 00 c0"), False),
    ("Usage Page given after the usage", H("0b 02 00 01 00 a1 01 09 30 05 01 15 81 25 7f 75 08 95 01 81 06 c0"), False),
    ("logical range too wide for the size", RESMUL.replace(H("25 01 35 00 45 0f 95 01 75 02 b1 02"), H("25 0f 35 00 45 0f 95 01 75 02 b1 02")), True),
    ("Usage Minimum > Usage Maximum", H("05 01 09 02 a1 01 05 09 19 32 29 30 15 00 25 01 95 08 75 01 81 02 c0"), True),
    ("Usage Minimum without Maximum", H("05 01 09 02 a1 01 05 09 19 01 15 00 25 01 95 08 75 01 81 02 c0"), True),
    ("extended Usage Min/Max on different pages", H("05 01 09 02 a1 01 1b 01 00 09 00 2b 08 00 07 00 15 00 25 01 95 08 75 01 81 02 c0"), True),
    ("Report ID tag with only an ID-less report", H("05 01 09 02 a1 01 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 85 02 c0"), True),
    ("wheel without its Usage", RESMUL.replace(H("09 38 15 81"), H("15 81")), True),
    ("multiplier without its Usage", RESMUL.replace(H("09 48 15 00"), H("15 00")), True),
    ("top-level collection without a Usage", H("05 01 a1 01 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 c0"), True),
    ("top-level Physical collection", H("05 01 09 02 a1 00 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 c0"), True),
    ("Report Count 0", H("05 01 09 02 a1 01 05 09 19 01 29 08 15 00 25 01 95 00 75 01 81 02 c0"), True),
    ("Delimiter left open", H("05 01 09 02 a1 01 a9 01 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 c0"), True),
    ("Push with data", H("05 01 09 02 a1 01 a5 01 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 b4 c0"), True),
    ("long item missing its tag", H("05 01 09 06 a1 01 05 07 19 00 29 ff 15 00 25 ff 95 01 75 08 81 00 c0 fe 00"), True),
    ("long item missing its data", H("05 01 09 06 a1 01 05 07 19 00 29 ff 15 00 25 ff 95 01 75 08 81 00 c0 fe 10 f0"), True),
    ("Pop back to no report ID", H("05 01 09 06 a1 01 a4 85 01 05 07 19 00 29 01 15 00 25 01 95 08 75 01 81 00 b4 95 08 75 01 81 00 c0"), True),
    ("report ID 256", H("05 01 09 06 a1 01 86 00 01 05 07 19 00 29 01 15 00 25 01 95 08 75 01 81 00 c0"), True),
    ("extended usages only", H("0b 06 00 01 00 a1 01 1b 00 00 07 00 2b ff 00 07 00 15 00 25 ff 95 01 75 08 81 00 c0"), False),
    (
        "same ID in two top-level collections",
        H("05 01 09 02 a1 01 85 01 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 c0")
        + H("05 01 09 06 a1 01 85 01 05 07 19 e0 29 e7 15 00 25 01 95 08 75 01 81 02 c0"),
        True,
    ),
    ("field spanning 5 bytes", H("05 01 09 02 a1 01 85 01 95 01 75 01 81 01 05 01 09 30 15 00 27 ff ff ff ff 95 01 75 20 81 02 95 01 75 07 81 01 c0"), True),
    ("collection left open", RESMUL[:-1], True),
    ("resolution multiplier", RESMUL, False),
]


def main():
    failed = 0
    for name, data, expect_error in CASES:
        _, errors, _ = h.check(name, data, 32)
        ok = bool(errors) == expect_error
        failed += not ok
        print(("PASS" if ok else "FAIL"), name, errors)

    lines, errors, _ = h.check("unit exponent", H("05 01 09 02 a1 01 55 0e 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 c0"), 32)
    ok = not errors and any("Unit Exponent (-2)" in line for line in lines)
    failed += not ok
    print(("PASS" if ok else "FAIL"), "unit exponent 0x0E reads as -2")

    if failed:
        print(f"{failed} case(s) failed")
        return 1
    print("all cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
