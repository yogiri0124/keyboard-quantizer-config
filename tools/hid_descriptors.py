#!/usr/bin/env python3
"""Pull the HID report descriptors out of a QMK .elf, check them, print them.

Runs in CI after the build so a malformed descriptor fails the build instead
of reaching the device (Windows caches descriptors per VID/PID/version, so a
bad one is painful to recover from). Standard library only.

    hid_descriptors.py firmware.elf [--max-report-bytes N]

Exit status is 1 if any descriptor has an error.
"""

import argparse
import struct
import sys

# Descriptor arrays defined in tmk_core/protocol/usb_descriptor.c. Only the
# ones the build actually contains are reported.
SYMBOLS = ["KeyboardReport", "MouseReport", "SharedReport", "RawReport", "ConsoleReport", "JoystickReport", "DigitizerReport"]


# --- ELF ---------------------------------------------------------------------


def read_symbols(path, names):
    """Return {name: bytes} for the named data symbols of a 32-bit LE ELF."""
    with open(path, "rb") as f:
        elf = f.read()
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit(f"{path}: not a 32-bit little-endian ELF")

    e_shoff, = struct.unpack_from("<I", elf, 0x20)
    e_shentsize, e_shnum = struct.unpack_from("<HH", elf, 0x2E)
    sections = []
    for i in range(e_shnum):
        # name, type, flags, addr, offset, size, link, info, align, entsize
        sections.append(struct.unpack_from("<IIIIIIIIII", elf, e_shoff + i * e_shentsize))

    SHT_SYMTAB, SHT_NOBITS = 2, 8
    found = {}
    for sh in sections:
        if sh[1] != SHT_SYMTAB:
            continue
        strtab = sections[sh[6]]
        for off in range(sh[4], sh[4] + sh[5], 16):
            st_name, st_value, st_size, st_info, _, st_shndx = struct.unpack_from("<IIIBBH", elf, off)
            end = elf.index(b"\0", strtab[4] + st_name)
            name = elf[strtab[4] + st_name : end].decode()
            if name not in names or st_size == 0 or st_shndx == 0 or st_shndx >= len(sections):
                continue
            sec = sections[st_shndx]
            if sec[1] == SHT_NOBITS:
                raise SystemExit(f"{name}: lives in a NOBITS section, cannot read its bytes")
            start = sec[4] + (st_value - sec[3])
            found[name] = elf[start : start + st_size]
    return found


# --- HID items ---------------------------------------------------------------

MAIN, GLOBAL, LOCAL = 0, 1, 2

ITEM_NAMES = {
    (MAIN, 0x8): "Input",
    (MAIN, 0x9): "Output",
    (MAIN, 0xB): "Feature",
    (MAIN, 0xA): "Collection",
    (MAIN, 0xC): "End Collection",
    (GLOBAL, 0x0): "Usage Page",
    (GLOBAL, 0x1): "Logical Minimum",
    (GLOBAL, 0x2): "Logical Maximum",
    (GLOBAL, 0x3): "Physical Minimum",
    (GLOBAL, 0x4): "Physical Maximum",
    (GLOBAL, 0x5): "Unit Exponent",
    (GLOBAL, 0x6): "Unit",
    (GLOBAL, 0x7): "Report Size",
    (GLOBAL, 0x8): "Report ID",
    (GLOBAL, 0x9): "Report Count",
    (GLOBAL, 0xA): "Push",
    (GLOBAL, 0xB): "Pop",
    (LOCAL, 0x0): "Usage",
    (LOCAL, 0x1): "Usage Minimum",
    (LOCAL, 0x2): "Usage Maximum",
    (LOCAL, 0x3): "Designator Index",
    (LOCAL, 0x4): "Designator Minimum",
    (LOCAL, 0x5): "Designator Maximum",
    (LOCAL, 0x7): "String Index",
    (LOCAL, 0x8): "String Minimum",
    (LOCAL, 0x9): "String Maximum",
    (LOCAL, 0xA): "Delimiter",
}

SIGNED = {(GLOBAL, 0x1), (GLOBAL, 0x2), (GLOBAL, 0x3), (GLOBAL, 0x4), (GLOBAL, 0x5)}

PAGES = {0x01: "Generic Desktop", 0x07: "Keyboard", 0x08: "LED", 0x09: "Button", 0x0C: "Consumer"}

USAGES = {
    0x01: {
        0x01: "Pointer",
        0x02: "Mouse",
        0x04: "Joystick",
        0x05: "Gamepad",
        0x06: "Keyboard",
        0x30: "X",
        0x31: "Y",
        0x32: "Z",
        0x38: "Wheel",
        0x48: "Resolution Multiplier",
        0x80: "System Control",
    },
    0x0C: {0x01: "Consumer Control", 0x238: "AC Pan"},
}

COLLECTIONS = {0: "Physical", 1: "Application", 2: "Logical", 3: "Report", 4: "Named Array", 5: "Usage Switch", 6: "Usage Modifier"}

REPORT_KINDS = {0x8: "Input", 0x9: "Output", 0xB: "Feature"}


def usage_name(page, usage):
    if page >= 0xFF00:
        return f"0x{usage:04X}"
    return USAGES.get(page, {}).get(usage, f"0x{usage:04X}")


def page_name(page):
    if page >= 0xFF00:
        return f"Vendor 0x{page:04X}"
    return PAGES.get(page, f"0x{page:04X}")


def main_flags(value):
    bits = [
        "Const" if value & 0x01 else "Data",
        "Var" if value & 0x02 else "Array",
        "Rel" if value & 0x04 else "Abs",
    ]
    if value & 0x08:
        bits.append("Wrap")
    if value & 0x10:
        bits.append("NonLinear")
    if value & 0x20:
        bits.append("NoPref")
    if value & 0x40:
        bits.append("Null")
    if value & 0x100:
        bits.append("Buffered")
    return ",".join(bits)


class Report:
    def __init__(self, kind, report_id):
        self.kind = kind
        self.report_id = report_id
        self.bits = 0
        self.fields = []  # (page, [usages] or (min, max), size, count, flags)


def check(name, data, max_report_bytes):
    """Parse one descriptor. Returns (lines, errors, reports)."""
    lines, errors = [], []
    glob = {"page": None, "lmin": None, "lmax": None, "size": None, "count": None, "id": None}
    stack = []
    local = {"usages": [], "umin": None, "umax": None}
    depth = 0
    uses_ids = False
    main_seen_before_id = False
    reports = {}
    pos = 0

    def err(msg):
        errors.append(f"{name} @0x{pos:03X}: {msg}")

    while pos < len(data):
        prefix = data[pos]
        if prefix == 0xFE:  # long item
            if pos + 1 >= len(data):
                err("truncated long item")
                break
            size = data[pos + 1]
            lines.append(f"{'  ' * depth}Long item ({size} bytes)")
            pos += 3 + size
            continue

        size = (0, 1, 2, 4)[prefix & 0x3]
        kind = (prefix >> 2) & 0x3
        tag = prefix >> 4
        if pos + 1 + size > len(data):
            err(f"item 0x{prefix:02X} needs {size} data bytes but the descriptor ends")
            break
        raw = data[pos + 1 : pos + 1 + size]
        value = int.from_bytes(raw, "little") if size else 0
        if (kind, tag) in SIGNED and size and value & (1 << (size * 8 - 1)):
            value -= 1 << (size * 8)
        item = ITEM_NAMES.get((kind, tag))
        indent = "  " * depth

        if kind == 3 or item is None:
            err(f"reserved item 0x{prefix:02X}")
            text = f"?? 0x{prefix:02X}"
        elif kind == GLOBAL:
            if tag == 0x0:
                glob["page"] = value
                text = f"{item} ({page_name(value)})"
            elif tag == 0x1:
                glob["lmin"] = value
                text = f"{item} ({value})"
            elif tag == 0x2:
                glob["lmax"] = value
                text = f"{item} ({value})"
            elif tag == 0x7:
                glob["size"] = value
                text = f"{item} ({value})"
                if value == 0 or value > 32:
                    err(f"report size {value} is outside 1-32")
            elif tag == 0x8:
                if value == 0:
                    err("report ID 0 is reserved")
                if main_seen_before_id:
                    err("report ID used after main items that had no report ID")
                glob["id"] = value
                uses_ids = True
                text = f"{item} ({value})"
            elif tag == 0x9:
                glob["count"] = value
                text = f"{item} ({value})"
            elif tag == 0xA:
                stack.append(dict(glob))
                text = item
            elif tag == 0xB:
                if not stack:
                    err("Pop without Push")
                else:
                    glob = stack.pop()
                text = item
            else:
                text = f"{item} ({value})"
        elif kind == LOCAL:
            page = glob["page"]
            if tag == 0x0:
                # A 4-byte usage carries its own page in the high half.
                if size == 4:
                    page, value = value >> 16, value & 0xFFFF
                local["usages"].append((page, value))
                text = f"{item} ({usage_name(page, value)})"
            elif tag == 0x1:
                local["umin"] = value
                text = f"{item} ({value})"
            elif tag == 0x2:
                local["umax"] = value
                text = f"{item} ({value})"
            else:
                text = f"{item} ({value})"
        else:  # MAIN
            if tag == 0xA:
                text = f"{item} ({COLLECTIONS.get(value, f'0x{value:02X}')})"
                lines.append(indent + text)
                depth += 1
                local = {"usages": [], "umin": None, "umax": None}
                pos += 1 + size
                continue
            if tag == 0xC:
                depth -= 1
                if depth < 0:
                    err("End Collection without Collection")
                    depth = 0
                lines.append("  " * depth + item)
                local = {"usages": [], "umin": None, "umax": None}
                pos += 1 + size
                continue

            # Input / Output / Feature
            if glob["size"] is None or glob["count"] is None:
                err(f"{item} before Report Size and Report Count")
            if glob["page"] is None and not (value & 0x01):
                err(f"{item} without a Usage Page")
            if glob["lmin"] is not None and glob["lmax"] is not None and glob["lmin"] > glob["lmax"] and not (value & 0x01):
                err(f"{item}: Logical Minimum {glob['lmin']} > Logical Maximum {glob['lmax']}")
            if not uses_ids:
                main_seen_before_id = True
            key = (tag, glob["id"] or 0)
            rep = reports.setdefault(key, Report(REPORT_KINDS[tag], glob["id"] or 0))
            bits = (glob["size"] or 0) * (glob["count"] or 0)
            rep.bits += bits
            if local["umin"] is not None and local["umax"] is not None:
                usages = (local["umin"], local["umax"])
            else:
                usages = [usage_name(p, u) for p, u in local["usages"]]
            rep.fields.append((glob["page"], usages, glob["size"], glob["count"], main_flags(value), glob["lmin"], glob["lmax"]))
            text = f"{item} ({main_flags(value)})  size={glob['size']} count={glob['count']}"
            local = {"usages": [], "umin": None, "umax": None}

        lines.append(indent + text)
        pos += 1 + size

    if depth != 0:
        errors.append(f"{name}: {depth} Collection(s) left open")
    if stack:
        errors.append(f"{name}: {len(stack)} Push without Pop")

    for rep in reports.values():
        if rep.bits % 8:
            errors.append(f"{name}: {rep.kind} report {rep.report_id} is {rep.bits} bits, not a whole number of bytes")
        total = rep.bits // 8 + (1 if rep.report_id else 0)
        if max_report_bytes and rep.kind == "Input" and total > max_report_bytes:
            errors.append(f"{name}: Input report {rep.report_id} is {total} bytes, over the {max_report_bytes}-byte endpoint")

    return lines, errors, reports


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("--max-report-bytes", type=int, default=0, help="fail if an Input report (with its ID) is larger")
    args = ap.parse_args()

    descriptors = read_symbols(args.elf, set(SYMBOLS))
    if not descriptors:
        print(f"{args.elf}: no HID report descriptors found", file=sys.stderr)
        return 1

    all_errors = []
    for name in SYMBOLS:
        if name not in descriptors:
            continue
        data = descriptors[name]
        lines, errors, reports = check(name, data, args.max_report_bytes)
        all_errors += errors
        print(f"== {name} ({len(data)} bytes)")
        print("   " + data.hex(" "))
        for line in lines:
            print("   " + line)
        print("   Reports:")
        for (_, _), rep in sorted(reports.items(), key=lambda kv: (kv[1].report_id, kv[0][0])):
            length = rep.bits // 8 + (1 if rep.report_id else 0)
            print(f"     {rep.kind:<7} id={rep.report_id:<3} {length:>3} bytes")
            for page, usages, size, count, flags, lmin, lmax in rep.fields:
                if isinstance(usages, tuple):
                    what = f"usage {usages[0]}-{usages[1]}"
                elif usages:
                    what = ", ".join(usages)
                else:
                    what = "(padding)" if "Const" in flags else "(no usage)"
                print(f"       {page_name(page) if page is not None else '-'}: {what}  [{size}b x{count} {flags} {lmin}..{lmax}]")
        print()

    if all_errors:
        print("HID descriptor errors:", file=sys.stderr)
        for e in all_errors:
            print("  " + e, file=sys.stderr)
        return 1
    print("HID descriptors OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
