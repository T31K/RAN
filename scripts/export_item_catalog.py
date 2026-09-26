#!/usr/bin/env python3
"""Export the item catalog (names, getitem codes, icons) for t31k.com/ran/items.

Reads the played client's data directly:
  client/data/glogic/glogic.rcc   (plain ZIP)
    ItemStrTable.txt  -> 4-byte header + AES-256-ECB, IN_<MID>_<SID> names
    item.isf          -> 128B type + DWORD ver, body byte-substituted (compbyte)
  client/textures/gui/*.dds       -> 512x256 icon sheets, 35px cells

Writes:
  <site>/app/ran/items/items.json
  <site>/public/ran-icons/<sheet>.jpg

Usage: scripts/export_item_catalog.py [--client ~/Projects/RAN/client] [--site ~/Projects/t31k.com]
"""

import argparse
import json
import os
import re
import struct
import subprocess
import zipfile

AES_KEY = "736b726b7268746c76646a214023777066726d666a677270676f21402324716b"

# compbyte::ARRAY from [Lib]__Engine/Sources/Common/CompByte.cpp (encode table).
COMPBYTE = bytes([
    0xCD, 0xF0, 0xA2, 0xE2, 0xED, 0x88, 0xE3, 0x98, 0xBC, 0x66, 0xC1, 0x7C, 0xF4, 0xDB, 0x47, 0x96,
    0xF6, 0x6C, 0x5E, 0x11, 0xAF, 0x2F, 0x40, 0x42, 0x41, 0x07, 0xDE, 0x4C, 0x8A, 0x63, 0x4D, 0x51,
    0xC0, 0x9B, 0x38, 0x27, 0x19, 0x03, 0x97, 0x65, 0x3D, 0x44, 0xAC, 0xA7, 0x18, 0xA0, 0x61, 0x13,
    0xB3, 0xB4, 0xC6, 0x21, 0x15, 0xE0, 0xC5, 0x0F, 0x78, 0xC4, 0xEF, 0x2C, 0x53, 0x26, 0x2E, 0x67,
    0x54, 0x5F, 0xD5, 0xC8, 0xAA, 0x17, 0x46, 0x95, 0xA3, 0x94, 0xFB, 0xBA, 0xD3, 0xBD, 0x64, 0x2A,
    0xBF, 0x34, 0x48, 0x35, 0x43, 0xD7, 0xF5, 0xCF, 0x90, 0x92, 0x2D, 0xB5, 0x5D, 0x93, 0x99, 0x50,
    0x74, 0x72, 0x31, 0x04, 0x58, 0x10, 0x5A, 0x7F, 0xFF, 0xCA, 0x55, 0x37, 0xB2, 0xDD, 0xE5, 0x0A,
    0x0D, 0x69, 0xB1, 0x3A, 0x00, 0x3C, 0xEA, 0x22, 0x32, 0x8D, 0xF2, 0x9C, 0x86, 0x1C, 0xB0, 0x76,
    0x30, 0x01, 0xD2, 0x06, 0xBB, 0x77, 0xF3, 0x80, 0xE8, 0xA6, 0x05, 0xEC, 0x89, 0x49, 0xFD, 0xD9,
    0xD6, 0xD4, 0x45, 0x6F, 0x4F, 0xB8, 0x33, 0x57, 0xD8, 0x87, 0xA8, 0x9E, 0xF9, 0x5C, 0x23, 0xB6,
    0x6B, 0xEB, 0x7E, 0x1F, 0x02, 0xFE, 0x85, 0xE9, 0x12, 0xC9, 0xAE, 0x08, 0x9F, 0x52, 0x25, 0x71,
    0x09, 0x3F, 0x29, 0x68, 0x3B, 0x1A, 0xE7, 0x91, 0x59, 0x7A, 0x6E, 0x8E, 0x56, 0xA4, 0x1D, 0x1E,
    0xA9, 0x9A, 0xDF, 0x70, 0x8C, 0x4E, 0x4B, 0xDC, 0xEE, 0x36, 0x8F, 0xC3, 0x83, 0x82, 0xE4, 0x8B,
    0x79, 0x6D, 0x0C, 0xA1, 0x7D, 0x39, 0x4A, 0xBE, 0xFA, 0xAB, 0xD1, 0xC7, 0x28, 0x7B, 0xCC, 0xF7,
    0xB7, 0xAD, 0x62, 0xB9, 0xD0, 0xF8, 0xF1, 0x0E, 0x1B, 0xCB, 0xDA, 0xE6, 0x2B, 0x60, 0x16, 0xC2,
    0x81, 0x0B, 0x73, 0xA5, 0x20, 0x84, 0x5B, 0x24, 0x9D, 0x75, 0xE1, 0xCE, 0x14, 0x6A, 0x3E, 0xFC,
])
assert len(COMPBYTE) == 256
DECODE = bytes(COMPBYTE.index(i) for i in range(256))

ICON_PX = 35
SHEET_W, SHEET_H = 512, 256

# Category headers that are placeholders or wrong in ItemStrTable.
CATEGORY_NAMES = {
    4: "HP Potions", 5: "MP Potions", 6: "SP Potions", 8: "Vitality Potions",
    13: "Arrows", 14: "Script Arrows", 18: "Refine Stones", 21: "Mid Necklaces",
    22: "Vitality Potions II", 38: "Sacred Gate Female Coat", 41: "Sacred Gate Female Pants",
    65: "Misc Items", 77: "Rainbow Weapons", 110: "Event Placeholders", 125: "Premium Weapons [15D]",
    133: "Event Etc", 144: "Rebuild Cards", 152: "Brawler Scrolls II", 153: "Swordsman Scrolls II",
    154: "Archer Scrolls II", 155: "Shaman Scrolls II", 156: "Supreme Brook Armor",
    157: "Rare Supreme Brook Armor", 159: "Rare Supreme Weapons",
}
# In these categories SID 0 is a real item, not a header.
REAL_SID0 = {4, 5, 6, 8, 13, 14}


def group(mid):
    if mid in (0, 3, 13, 14, 19, 23, 27, 28, 29, 30, 31, 68, 69, 70, 74, 75, 77, 79, 81, 82,
               97, 98, 99, 100, 101, 102, 158, 159):
        return "Weapons"
    if 32 <= mid <= 58 or 85 <= mid <= 96 or mid in (156, 157):
        return "Armor"
    if 59 <= mid <= 62 or mid == 21:
        return "Accessories"
    if mid in (4, 5, 6, 8, 22, 111):
        return "Potions"
    if mid in (11, 12, 15, 16, 17, 18, 143, 144, 201, 202):
        return "Upgrades"
    if mid in (71, 72, 73, 80, 152, 153, 154, 155):
        return "Skill Scrolls"
    if 103 <= mid <= 110:
        return "Quest"
    if 112 <= mid <= 126 or mid in (150, 151, 160, 190, 191, 192, 193):
        return "Premium & Costumes"
    if mid in (142, 161, 162):
        return "Pets & Boards"
    if 180 <= mid < 200:
        return "Regional"
    return "Misc"


def is_junk(name):
    return not name or name == "Temp" or set(name) <= set(".[]0123456789 ")


def read_names(rcc):
    raw = rcc.read("ItemStrTable.txt")[4:]
    plain = subprocess.run(
        ["openssl", "enc", "-aes-256-ecb", "-d", "-nopad", "-K", AES_KEY],
        input=raw, capture_output=True, check=True,
    ).stdout.decode("cp1252", errors="replace")
    names = {}
    for line in plain.splitlines():
        m = re.match(r"IN_(\d+)_(\d+)\t(.*)", line)
        if m:
            names[(int(m[1]), int(m[2]))] = re.sub(r"\s+", " ", m[3]).strip()
    return names


def read_icons(rcc):
    """SITEM records -> {(mid, sid): (sheet, ix, iy)} from each FILE_SBASIC chunk (ver 0x114)."""
    raw = rcc.read("item.isf")
    body = raw[132:].translate(DECODE)
    icons = {}
    for m in re.finditer(rb"\x01\x00\x00\x00\x14\x01\x00\x00", body):
        o = m.end() + 4  # skip chunk size

        def u32():
            nonlocal o
            v = struct.unpack_from("<I", body, o)[0]
            o += 4
            return v

        def string():
            nonlocal o
            n = u32()
            v = body[o:o + n].split(b"\0")[0].decode("latin1")
            o += n
            return v

        try:
            nid = u32()
            u32()  # sGroupID
            if not string().startswith("IN_"):
                continue
            # emLevel, grades, fExpMultiple, reserved, flags/prices/type/reqs, req levels, sReqStats, inven size
            o += 4 + 4 + 4 + 10 + 4 * 7 + 8 + 12 + 4
            icon = u32()
            files = [string() for _ in range(5)]
        except struct.error:
            continue
        icons[(nid & 0xFFFF, nid >> 16)] = (files[4].lower(), icon & 0xFFFF, icon >> 16)
    return icons


def export_sheets(needed, client, out_dir):
    gui = os.path.join(client, "textures", "gui")
    by_lower = {f.lower(): f for f in os.listdir(gui)}
    os.makedirs(out_dir, exist_ok=True)
    exported = []
    for sheet in sorted(needed):
        src = by_lower.get(sheet)
        if not sheet.endswith(".dds") or not src:
            continue
        name = sheet[:-4]
        subprocess.run(
            ["ffmpeg", "-v", "error", "-y", "-i", os.path.join(gui, src),
             "-vf", "format=rgb24", "-q:v", "3", os.path.join(out_dir, name + ".jpg")],
            check=True,
        )
        exported.append(sheet)
    return exported


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--client", default=os.path.expanduser("~/Projects/RAN/client"))
    ap.add_argument("--site", default=os.path.expanduser("~/Projects/t31k.com"))
    args = ap.parse_args()

    with zipfile.ZipFile(os.path.join(args.client, "data", "glogic", "glogic.rcc")) as rcc:
        names = read_names(rcc)
        icons = read_icons(rcc)

    sheets = export_sheets({s for s, _, _ in icons.values()}, args.client,
                           os.path.join(args.site, "public", "ran-icons"))
    sheet_index = {s: i for i, s in enumerate(sheets)}

    categories = {}
    for (mid, sid), name in sorted(names.items()):
        if (sid == 0 and mid not in REAL_SID0) or is_junk(name):
            continue
        row = [sid, name]
        icon = icons.get((mid, sid))
        if icon and icon[0] in sheet_index and icon[1] * ICON_PX < SHEET_W and icon[2] * ICON_PX < SHEET_H:
            row += [sheet_index[icon[0]], icon[1], icon[2]]
        categories.setdefault(mid, []).append(row)

    out = []
    for mid, items in categories.items():
        name = CATEGORY_NAMES.get(mid) or names.get((mid, 0), "")
        if is_junk(name) or name.startswith("IN_") or name == "?":
            name = f"Category {mid}"
        out.append({"mid": mid, "name": name, "group": group(mid), "items": items})

    data = {"sheets": [s[:-4] for s in sheets], "categories": out}
    path = os.path.join(args.site, "app", "ran", "items", "items.json")
    with open(path, "w") as f:
        json.dump(data, f, separators=(",", ":"), ensure_ascii=False)

    total = sum(len(c["items"]) for c in out)
    with_icon = sum(len(r) > 2 for c in out for r in c["items"])
    print(f"{total} items in {len(out)} categories, {with_icon} with icons, {len(sheets)} sheets -> {path}")


if __name__ == "__main__":
    main()
