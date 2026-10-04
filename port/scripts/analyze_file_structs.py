#!/usr/bin/env python3
"""Static layout check of the on-disk types (until CI provides the MSVC x86 golden sizes).

Input: port/build/file_structs.json from gen_file_struct_sizes.py.
For the dumper TU (port/tests/golden/file_struct_sizes.cpp) and for every .cpp that defines a
listed type locally, clang dumps the record layouts (-Xclang -fdump-record-layouts) twice:

  native  arm64-apple-macos (what the port runs)                     -> port/build/layouts_native.txt
  ilp32   i386-apple-macos -malign-double -mms-bitfields             -> port/build/layouts_ilp32.txt
          (a proxy for MSVC x86: 4-byte pointers and long, 8-aligned double/long long in
          structs like MSVC, MSVC bitfield packing; #pragma pack is honoured by both)

The proxy differs from MSVC in three known ways, which are checked by spelling/shape instead:
wchar_t (4 here, 2 on Windows), time_t (4 here, 8 on MSVC), and Itanium tail-padding reuse of
non-POD bases (MSVC never reuses a base's tail padding).

Output: docs/port/file-structs.md (table: struct, header, reader call site, native size, ILP32
size, suspicious members, verdict).
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "port/build"
JSON = BUILD / "file_structs.json"
DOC = ROOT / "docs/port/file-structs.md"
DUMPER = "port/tests/golden/file_struct_sizes.cpp"

TARGETS = {
    "native": [],
    "ilp32": ["-target", "i386-apple-macos10.13", "-malign-double", "-mms-bitfields"],
}
LIBRARY = re.compile(r"\bstd::|\bCString[AW]?\b|\bbasic_string\b")
RISKY_SPELLING = [
    (re.compile(r"\*|\(\*\)"), "pointer"),
    (re.compile(r"\b(unsigned )?long\b(?! long)"), "long"),
    (re.compile(r"\bsize_t\b|\bptrdiff_t\b|\bu?intptr_t\b|_PTR\b|\bSIZE_T\b|\bLPARAM\b|\bWPARAM\b"), "pointer-sized integer"),
    (re.compile(r"(?<!__)\btime_t\b"), "time_t (8 on MSVC and arm64; the ILP32 proxy says 4)"),
    (re.compile(r"\bwchar_t\b"), "wchar_t (2 on Windows, 4 on macOS)"),
    (re.compile(r"\blong double\b"), "long double"),
    (LIBRARY, "library object"),
    (re.compile(r"^(struct |class )?(LP[A-Z0-9_]+|P(BYTE|VOID|CHAR|STR|WSTR|DWORD|WORD)|H(WND|ANDLE|MODULE|INSTANCE|DC|BITMAP|FONT|ICON|CURSOR|BRUSH|PEN|RGN|KEY|MENU))$"),
     "pointer typedef / handle"),
]


def includes():
    subprocess.run([str(ROOT / "port/scripts/compile_one.sh"), "port/tests/golden/struct_sizes.cpp", "1"],
                   cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    incs = ["-I" + str(ROOT / "port/compat/include"), "-I" + str(ROOT / "port/third_party/dxvk-install/include/dxvk")]
    incs += ["-I" + d for d in (BUILD / "ran_incs.txt").read_text().splitlines() if d]
    odbc = subprocess.run(["brew", "--prefix", "unixodbc"], capture_output=True, text=True).stdout.strip()
    if odbc:
        incs += ["-idirafter", odbc + "/include"]
    sdl = subprocess.run(["brew", "--prefix", "sdl3"], capture_output=True, text=True).stdout.strip()
    if sdl:
        incs += ["-idirafter", sdl + "/include"]
    return incs


def dump(tu, target_flags, incs):
    sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
    cmd = ["clang++", *target_flags, "-isysroot", sdk, "-std=c++14", "-fms-extensions", "-fdeclspec",
           "-Wno-everything", "-DNDEBUG", "-D_LIB", *incs, "-fsyntax-only", "-Xclang", "-fdump-record-layouts", tu]
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    if p.returncode != 0:
        print(f"warning: {tu} {' '.join(target_flags) or 'native'}: clang failed\n{p.stderr[-2000:]}", file=sys.stderr)
    return p.stdout


LINE = re.compile(r"^\s*(\d+)(?::(\d+)-(\d+))?\s*\|(\s*)(.*)$")


def parse(text):
    """-> {record name: {size, align, fields: [(offset, depth, text, bitfield)]}} (first dump wins)."""
    out = {}
    for blk in text.split("*** Dumping AST Record Layout")[1:]:
        rows = blk.strip("\n").split("\n")
        m = LINE.match(rows[0])
        if not m:
            continue
        name = re.sub(r"^(struct|class|union) ", "", m.group(5).strip())
        fields = []
        size = align = None
        for r in rows[1:]:
            mm = LINE.match(r)
            if mm:
                fields.append((int(mm.group(1)), (len(mm.group(4)) - 1) // 2, mm.group(5).strip(), bool(mm.group(2))))
                continue
            ms = re.search(r"sizeof=(\d+).*?align=(\d+)", r)
            if ms:
                size, align = int(ms.group(1)), int(ms.group(2))
            if "nvalign" in r or "]" in r:
                if size is not None:
                    break
        if name not in out and size is not None:
            out[name] = {"size": size, "align": align, "fields": fields}
    return out


def member_type(txt):
    return re.sub(r"\s+\w+$", "", txt.replace(" (empty)", "").replace(" (base)", ""))


def leaves(fields):
    """Leaf members (no nested rows below them). Library objects (std::string, CString, ...) are
    one opaque leaf: their internals differ per target and only matter as 'not serialisable'."""
    res = []
    skip = None
    for k, (off, depth, txt, bf) in enumerate(fields):
        if skip is not None:
            if depth > skip:
                continue
            skip = None
        nxt = fields[k + 1] if k + 1 < len(fields) else None
        if LIBRARY.search(member_type(txt)) and "(base)" not in txt:
            res.append([off, depth, txt, bf])
            skip = depth
            continue
        if nxt and nxt[1] > depth:
            continue
        if "(empty)" in txt:
            continue
        res.append([off, depth, txt, bf])
    return res


def spans(lv, size):
    out = []
    for k, l in enumerate(lv):
        nxt = lv[k + 1][0] if k + 1 < len(lv) else size
        out.append(nxt - l[0])
    return out


def tail_padding_reuse(layout, layouts):
    """Base subobject whose tail padding is reused by the next member (Itanium only)."""
    f = layout["fields"]
    hits = []
    for k, (off, depth, txt, bf) in enumerate(f):
        m = re.match(r"(?:struct|class) (.+) \((?:primary )?base\)", txt)
        if not m:
            continue
        b = layouts.get(m.group(1))
        if not b or "(empty)" in txt:
            continue
        # the next member of the same record (same depth); leaving the record ends the search
        for off2, d2, txt2, _ in f[k + 1:]:
            if d2 < depth:
                break
            if d2 == depth:
                if off2 < off + b["size"]:
                    hits.append(f"`{m.group(1)}` base (sizeof {b['size']}) has its tail padding reused by `{txt2}` at {off2}")
                break
    return hits


def classify(txt):
    if "vtable pointer" in txt or "vftable pointer" in txt:
        return "vtable pointer"
    t = member_type(txt)   # drop the member name
    for rx, why in RISKY_SPELLING:
        if rx.search(t):
            return why
    return None


# Proposed fixes for the types that are genuinely different AND genuinely read from disk.
# (Not applied: game sources are CP949; see docs/port/README.md "Rules for changing game sources".)
FIXES = {
    "DXOCMATERIAL": "Octree map meshes (`DxOcMeshes::LoadFile`, DxOctreeMesh.cpp:568, reached from `DxLandMan` "
                    "octree loading). `pTexture` (LPDIRECT3DTEXTUREQ) is 4 bytes in the file. Read each element as "
                    "the Win32 image: `ReadBuffer(&m.rgMaterial, 68)`, skip a `DWORD` (old pointer), "
                    "`ReadBuffer(m.szTexture, MAX_PATH)` = 332 bytes/element; SaveFile writes a zero `DWORD` in the "
                    "pointer slot. Same for `DXMATERIAL_MULTITEX` (identical layout).",
    "DXMATERIAL_MULTITEX": "Map frame effect `DxEffectMultiTex::LoadBuffer/LoadBufferSet`. Same 68 + 4 + 260 = 332-byte "
                           "Win32 element as DXOCMATERIAL; per-element read with the pointer slot read into a `DWORD`.",
    "DXMATERIAL_NEON": "Map effect `DxEffectNeon::LoadBuffer/LoadBufferSet`. Win32 element (552): `BOOL bUse`, "
                       "`D3DXVECTOR2 vMoveTex, vScaleUV`, `D3DCOLOR vColor`, two 4-byte pointer slots "
                       "(`pSrcTex`, `pNeonTex`), `char szTexture[260], szNeonTex[260]`. Per-element read, pointers "
                       "read as `DWORD` and set to NULL.",
    "DXMATERIAL_SPEC2": "Map effect `DxEffectSpecular2::LoadBuffer*`. Win32 element (528): `BOOL bSpecUse`, 4-byte "
                        "pointer slot `pSpecTex`, `char szTexture[260], szSpecTex[260]`.",
    "DXMATERIAL_SPECREFLECT": "Map effect `DxEffectSpecReflect::LoadBuffer*`. Same Win32 element as DXMATERIAL_SPEC2 (528).",
    "DXMATERIAL_SPECULAR": "Old-version character effect materials (`DxEffCharLevel/MultiTex/Neon/Reflection2::LoadFile`). "
                           "Same 528-byte Win32 element as DXMATERIAL_SPEC2.",
    "DXUSERMATERIAL": "Map effect `DxEffectGlow::LoadBuffer*`. Win32 element (536): `BOOL bGlowUse, bColorUse`, "
                      "`D3DCOLOR cColor`, 4-byte pointer slot `pGlowTex`, `char szTexture[260], szGlowTex[260]`.",
    "DXMATERIAL_CHAR_EFF": "Character effects (`DxEffCharLevel/MultiTex/Neon/Reflection2/Specular2/TexDiff/UserColor`). "
                           "Win32 element (596): `BOOL bEffUse`, `D3DMATERIAL9 d3dMaterial` (68), 4-byte pointer slot "
                           "`pEffTex`, `char szTexture[260], szEffTex[260]`.",
    "DXMATERIAL_CHAR_EFF_100": "Old character effect materials. Win32 element (532): `BOOL bEffUse`, `float "
                               "fMaterial_Power`, 4-byte pointer slot `pEffTex`, `char szTexture[260], szEffTex[260]`.",
    "DxEffectTiling::POINTEX": "Map effect `DxEffectTiling::LoadBuffer*`. Win32 element (24): 4-byte pointer slot "
                               "`pPoint` (rebuilt after load), `D3DXVECTOR3 vPos`, `DWORD dwMaterial`, `DWORD dwColor`.",
    "EFFCHAR_PROPERTY_LINE2BONEEFF": "Character effect. Holds two `CMinMax<float>` members, a class with a vtable: "
                                     "12 bytes each on Win32 (vptr 4 + min + max), 16 on arm64, and a raw read "
                                     "overwrites the native vptr with a Windows address. Read the 232-byte Win32 image "
                                     "into a byte buffer and copy fields: offsets 0..75 as-is, `m_fMinMaxDist.min/max` "
                                     "from 80/84, `m_fMinMaxTexRotate.min/max` from 92/96, `m_bWithCamMove` at 100, "
                                     "`m_szTexture` at 101, `m_szTexture2` at 165 (use the file's dwSize, which the "
                                     "loader already reads).",
    "EFFANI_PROPERTY_TRACE": "Animation effect (EMEFFANI_TRACE, `DxEffAniData_Trace::LoadFile`; the source already says "
                             "\"// problem\"). The file holds an MSVC std::string image (24 bytes). Read `dwSize` bytes, "
                             "take `m_fScale` from offset 0 and the string from the MSVC SSO buffer at offset 4 when its "
                             "capacity field (offset 24) is < 16 (else empty). None of the shipped animation .cfg files "
                             "use this effect type (see below), so it is not urgent.",
    "EFFCHAR_PROPERTY_TEXDIFF": "Character effect with a `std::string m_strTex` read raw (garbage pointers on Windows "
                                "too unless SSO). Same size as MSVC (40) by coincidence, different layout. Same "
                                "treatment as EFFANI_PROPERTY_TRACE: parse the Win32 image (string at offset 16).",
    "EFFCHAR_PROPERTY_TEXDIFF_100": "Old TexDiff property: Win32 image 36 bytes (string at offset 12), native 40. "
                                    "Parse the Win32 image as for EFFCHAR_PROPERTY_TEXDIFF.",
    "TILE_TEX": "`DxBlend::LoadFile_Edit` - an editor-only path (std::string read raw). Not used by the client; leave.",
}


def write_doc(rows, data):
    def cell(s):
        return str(s).replace("|", "\\|").replace("\n", " ")

    flagged = [r for r in rows if r["verdict"] != "same" and not r["verdict"].startswith("scalar")]
    lines = [
        "# On-disk struct layouts (native arm64 vs Windows x86)",
        "",
        "Generated by `python3 port/scripts/analyze_file_structs.py` from the list made by",
        "`port/scripts/gen_file_struct_sizes.py` (do not edit by hand; regenerate). Companion of gate P1.3",
        "(network structs): these are the types the client reads from / writes to files as raw bytes.",
        "",
        "- **Definitive check**: `port/scripts/check-file-struct-sizes.sh` compares the native sizes with the",
        "  MSVC x86 sizes from Windows CI (artifact `file-sizes-win32` -> `port/tests/golden/file_sizes_win32.txt`).",
        "- **Until then (this file)**: clang record layouts, native arm64 vs an ILP32 proxy",
        "  (`i386-apple-macos -malign-double -mms-bitfields`: 4-byte pointers/long, MSVC double alignment and",
        "  bitfields). Known proxy gaps are handled separately: std::string (24 bytes on MSVC x86, the `x86`",
        "  column corrects for it), wchar_t, time_t, and Itanium tail-padding reuse (checked on the native layout).",
        "",
        f"Types found: **{len(data['types'])}** ({len(data['listed'])} measured by the dumper, "
        f"{len(data['excluded'])} not measured with a reason). Flagged: **{len(flagged)}**.",
        "",
        "Call kinds: `ReadBuffer`/`WriteBuffer` (CSerialFile, CSerialMemory, CByteStream), `fread`/`fwrite`,",
        "`stream-vector` (basestream `>> std::vector<T>`, raw sizeof(T) per element), `memcpy-from-file-buffer`,",
        "`size-check+memcpy` (`dwSize == sizeof(T)` then a raw copy, the DxEffect `SetProperty(PBYTE, size, ver)`",
        "pattern), `assert-only` (`GASSERT(sizeof(x)==dwSize)` before a field-by-field LOAD).",
        "",
        "## Flagged types",
        "",
        "| struct | header | reader call site | native | x86 (expected) | suspicious members | verdict |",
        "|---|---|---|---|---|---|---|",
    ]
    for r in flagged:
        s = r["site"]
        lines.append(f"| `{r['name']}` | {cell(r['header'])} | {cell(s['file'].split('/')[-1])}:{s['line']} "
                     f"`{cell(s['func'])}` ({r['nsites']} sites; {', '.join(r['via'])}) | {r['native']} | "
                     f"{r['x86'] if r['x86'] is not None else r['ilp32']} | {cell('; '.join(r['sus'][:5]))} | "
                     f"{cell(r['verdict'])}{(' - ' + cell('; '.join(r['notes']))) if r['notes'] else ''} |")
    lines += ["", "## Proposed fixes (not applied)", "",
              "Each fix keeps the Windows file format: read the Win32 image of the struct (pointer and vtable slots",
              "are 4 bytes, MSVC std::string 24 bytes) and copy fields; writers emit the same image. Map/landscape",
              "loaders first (a map load just failed): DXOCMATERIAL (octree), then the frame effects",
              "(MultiTex, Neon, Glow, Specular2, SpecReflect, Tiling) that `DxFrame::LoadEffect` -> `LoadBuffer` reads.",
              ""]
    order = ["DXOCMATERIAL", "DXMATERIAL_MULTITEX", "DXMATERIAL_NEON", "DXUSERMATERIAL", "DXMATERIAL_SPEC2",
             "DXMATERIAL_SPECREFLECT", "DxEffectTiling::POINTEX", "DXMATERIAL_CHAR_EFF", "DXMATERIAL_CHAR_EFF_100",
             "DXMATERIAL_SPECULAR", "EFFCHAR_PROPERTY_LINE2BONEEFF", "EFFANI_PROPERTY_TRACE",
             "EFFCHAR_PROPERTY_TEXDIFF", "EFFCHAR_PROPERTY_TEXDIFF_100", "TILE_TEX"]
    for k in order:
        r = next((x for x in rows if x["name"] == k), None)
        if r:
            lines.append(f"- **`{k}`** (native {r['native']}, Win32 {r['x86'] if r['x86'] is not None else r['ilp32']}): {FIXES[k]}")
    lines += ["", "## Animation .cfg (SANIMCONINFO) check, 2026-10-04", "",
              "All 2288 `.cfg` files in `client/data/animation/animation.rcc` (a plain zip; header = 128-byte",
              "`SANIMCONINFO` tag + DWORD version) were parsed with the Win32 layout: versions 0x107: 952,",
              "0x106: 881, 0x104: 278, 0x105: 167, 0x103: 5, 0x102: 5 (no 0x101). Every 0x105-0x107 file is consumed",
              "exactly to EOF (header, names, DWORDs, `m_wDivCount` + WORD[8], `m_wStrikeCount` + SANIMSTRIKE[9],",
              "SChaSoundData(_103), effects, DxAniScale). On disk `m_wDivCount` is 0 everywhere and",
              "`m_wStrikeCount` <= 7. Effects used: EMEFFANI_SINGLE v0x100 (400 bytes) x64, v0x101 (464) x45,",
              "EMEFFANI_GHOSTING v0x100 (28) x19; no TRACE or FACEOFF. Native sizes of every struct on this path",
              "equal the Win32 sizes (SANIMCONINFO_102 564, _103 732, _104 840, SANIMSTRIKE 12, SChaSoundData 388,",
              "SChaSoundData_103 428, EFFANI_PROPERTY_SINGLE 464, _100 400, GHOSTING 28), so a layout mismatch",
              "cannot produce out-of-range counts. Note: `SANIMCONINFO_102/103/104` have no initialiser for",
              "`m_wDivCount`/`m_wStrikeCount`, and `CSerialMemory::read` silently copies nothing when",
              "`offset+size > m_nBufSize` (ReadBuffer still returns TRUE): a short/failed read of a 0x102-0x104 file",
              "leaves stack garbage in the counts, which `operator=` then loops over.",
              ""]
    lines += ["", "## Scalar sizeof sites that differ", ""]
    for s in data["risky_sites"]:
        lines.append(f"- {s['file']}:{s['line']} `{s['func']}` {s['call']}(sizeof({s['expr']})) -> `{s['type']}`: {s['why']}")
    lines += ["", "Fixes: StaticSoundMan.cpp `SVecSound::LoadSet` reads `sizeof(long)` -> read a `LONG` (4 bytes);",
              "HelpDataMan.cpp `HELPNODE_SIZE` is `std::list::size_type` (size_t) written/read raw -> use a `DWORD`.",
              "", "## Not measured (with reason)", ""]
    for n, why in sorted(data["excluded"].items()):
        r = next((x for x in rows if x["name"] == n), None)
        extra = f" - static layout: native {r['native']}, ILP32 {r['ilp32']}, {r['verdict']}" if r and r["native"] else ""
        lines.append(f"- `{n}`: {why}{extra}")
    lines += ["", "## All types", "", "| struct | header | native | ILP32 proxy | verdict |", "|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| `{r['name']}` | {cell(r['header'])} | {r['native']} | {r['ilp32']} | {cell(r['verdict'])} |")
    DOC.write_text("\n".join(lines) + "\n")
    print(f"-> {DOC.relative_to(ROOT)}")


def main():
    data = json.loads(JSON.read_text())
    types, listed, excluded = data["types"], data["listed"], data["excluded"]
    incs = includes()
    tus = [DUMPER] + sorted({types[n]["header"] for n in excluded if types[n].get("header", "").endswith(".cpp")})
    lay = {}
    for tgt, flags in TARGETS.items():
        txt = ""
        for tu in tus:
            txt += dump(tu, flags, incs)
        (BUILD / f"layouts_{tgt}.txt").write_text(txt)
        lay[tgt] = parse(txt)

    def find(layouts, name, info):
        base = [name] + list(info.get("via_typedef", []))
        base += [re.sub(r"Q$", "9", b) for b in base]   # dxstdafx.h: #define D3DLIGHTQ D3DLIGHT9
        cands = [p + b for b in base for p in ("", "_", "tag", "_tag")]   # typedef struct _X {..} X
        cands += [b.lower() + "_tag" for b in base]                       # PCMWAVEFORMAT -> pcmwaveformat_tag
        short = name.split("::")[-1]
        for c in cands:
            if c in layouts:
                return c
        for k in layouts:   # function-local records are dumped unqualified
            if k == short or k.endswith("::" + short) and info.get("header", "").endswith(".cpp"):
                return k
        return None

    rows = []
    for name in sorted(types):
        info = types[name]
        if info["kind"] == "enum":
            continue
        key = find(lay["native"], name, info)
        nat = lay["native"].get(key) if key else None
        ilp = lay["ilp32"].get(key) if key else None
        sus, notes = [], []
        verdict = "same"
        x86 = None
        if nat is None:
            if info["kind"] == "scalar":
                verdict = "scalar (checked by the dumper)"
            else:
                verdict = "no layout (see note)"
                notes.append("clang did not dump a layout")
        else:
            ln, li = leaves(nat["fields"]), leaves(ilp["fields"]) if ilp else []
            sn = spans(ln, nat["size"])
            si = spans(li, ilp["size"]) if ilp else []
            paired = ilp is not None and len(ln) == len(li) and all(a[2] == b[2] for a, b in zip(ln, li))
            for k, l in enumerate(ln):
                why = classify(l[2])
                differs = paired and (sn[k] != si[k] or l[0] - ln[0][0] != li[k][0] - li[0][0])
                if why or (paired and sn[k] != si[k]):
                    detail = f"`{l[2]}` @{l[0]}"
                    if paired and sn[k] != si[k]:
                        detail += f" (span {si[k]}->{sn[k]})"
                    if why:
                        detail += f" [{why}]"
                    sus.append(detail)
            if l := tail_padding_reuse(nat, lay["native"]):
                notes += l
            # The proxy measures libc++'s 32-bit std::string (12 bytes); MSVC x86 (VS2010+) has 24.
            nstr = 0
            for l in li:
                m = re.match(r"std::string(?:\[(\d+)\])?\s", l[2])
                if m:
                    nstr += int(m.group(1) or 1)
            x86 = ilp["size"] + 12 * nstr if ilp else None
            if nstr:
                notes.append(f"{nstr} std::string: expected MSVC x86 size {x86} (24-byte std::string; the proxy says {ilp['size']})")
            if ilp is None:
                notes.append("no ILP32 layout")
            elif x86 != nat["size"]:
                verdict = "DIFFERENT SIZE"
            elif paired and any(a[0] != b[0] for a, b in zip(ln, li)):
                verdict = "DIFFERENT LAYOUT (same size)"
            elif not paired:
                notes.append("member lists differ between targets")
            if any("vtable" in s for s in sus):
                verdict = verdict + "; polymorphic" if verdict != "same" else "polymorphic (raw read overwrites the vptr)"
            if notes and any("tail padding" in x for x in notes):
                verdict = "DIFFERENT on MSVC (tail padding)" if verdict == "same" else verdict + "; tail padding"
            if verdict == "same" and any("time_t" in s or "wchar_t" in s for s in sus):
                verdict = "CHECK (see members)"
            if verdict == "same" and any("library" in s for s in sus):
                verdict = "same size, NOT serialisable (library object; bytes are garbage on Windows too)"
        vias = sorted({s["via"] for s in info["sites"]})
        if verdict.startswith("DIFFERENT") and vias == ["assert-only"]:
            verdict = "differs, but only compared in a GASSERT (no-op in Release); fields are LOADed one by one"
        site = info["sites"][0]
        rows.append({"name": name, "header": info.get("header", ""), "site": site, "nsites": len(info["sites"]),
                     "via": vias, "native": nat["size"] if nat else None, "ilp32": ilp["size"] if ilp else None,
                     "x86": x86, "sus": sus, "notes": notes, "verdict": verdict, "excluded": excluded.get(name)})
    (BUILD / "file_struct_analysis.json").write_text(json.dumps(rows, indent=1))
    write_doc(rows, data)
    bad = [r for r in rows if r["verdict"] not in ("same", "scalar (checked by the dumper)")]
    print(f"{len(rows)} types analysed, {len(bad)} flagged:")
    for r in bad:
        print(f"  {r['name']}: {r['verdict']} native {r['native']} ilp32 {r['ilp32']}  {'; '.join(r['sus'][:4])} {'; '.join(r['notes'])}")


if __name__ == "__main__":
    main()
