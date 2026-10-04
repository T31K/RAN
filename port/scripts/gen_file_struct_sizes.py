#!/usr/bin/env python3
"""Generates the on-disk struct list for the native port (companion of gen_struct_sizes.py).

The client reads many structs straight from files as raw bytes: CSerialFile/CByteStream
`ReadBuffer(&v, sizeof(T))` / `WriteBuffer`, the basestream `>> std::vector<T>` / `<< std::vector<T>`
templates (raw sizeof(T) per element), `fread/fwrite(&v, sizeof(T), ...)`, and memcpy out of
property blobs that were read from a file. Every such type must have the same size on Windows x86
(MSVC) and on macOS arm64, or every later read in the file is shifted.

This script tokenises the client sources (python3 port/scripts/client_sources.py) plus the headers
of the client projects, resolves each `sizeof(...)` argument of those calls to a declared type
(class members, locals, parameters, typedefs, base classes, member chains like `a.b->c`) and writes:

  port/tests/golden/file_struct_list.inc     RAN_FSIZE(<qualified name>) per type (+ comments for
                                             excluded types with the reason)
  port/tests/golden/file_struct_headers.inc  #include lines for the headers that declare them
  port/build/file_structs.json               every call site, for analyze_file_structs.py

The same .inc files are compiled by MSVC x86 in CI (golden sizes, artifact file-sizes-win32) and by
clang on macOS (port/tests/golden/file_struct_sizes.cpp via port/scripts/check-file-struct-sizes.sh).

Usage: gen_file_struct_sizes.py [--drop name ...]   (types that do not compile in the dumper are
dropped with --drop and recorded in the .inc; earlier drops are kept)
"""
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "port/tests/golden"
OUT_LIST = GOLDEN / "file_struct_list.inc"
OUT_HDRS = GOLDEN / "file_struct_headers.inc"
OUT_JSON = ROOT / "port/build/file_structs.json"

PROJECT_DIRS = ["[Client]__Game", "[Lib]__Engine", "[Lib]__EngineSound", "[Lib]__EngineUI", "[Lib]__MfcEx",
                "[Lib]__NetClient", "[Lib]__RanClient", "[Lib]__RanClientUI", "[Lib]__ZLib", "[Lib]__NetServer",
                "Dependency/common", "Dependency/NetGlobal"]

# Calls whose `sizeof` arguments describe bytes that go to/come from a file (or a buffer read
# from a file). memcpy-style calls only count inside functions that also do file IO, or when they
# fill a struct from a byte-pointer parameter (the DxEffect SetProperty(PBYTE, size, ver) pattern).
# Headers included before the generated list because other headers rely on them having been
# included by their .cpp first (DxEffectGlow.h uses DxMeshes from DxFrameMesh.h).
PRELUDE = ["[Lib]__Engine/Sources/DxMeshs/DxFrameMesh.h"]

IO_CALLS = {"ReadBuffer", "WriteBuffer", "fread", "fwrite"}
COPY_CALLS = {"memcpy", "CopyMemory", "MoveMemory", "memmove"}

# Scalar types. Builtins are identical on MSVC x86 and arm64 except the RISKY ones.
BUILTIN = {"char", "short", "int", "float", "double", "bool", "signed", "unsigned", "__int8", "__int16",
           "__int32", "__int64", "void"}
RISKY_BUILTIN = {"long": "long is 4 bytes on MSVC x86, 8 on macOS arm64",
                 "wchar_t": "wchar_t is 2 bytes on Windows, 4 on macOS",
                 "size_t": "size_t is 4 bytes on Win32, 8 on arm64",
                 "ptrdiff_t": "4 bytes on Win32, 8 on arm64",
                 "intptr_t": "4 bytes on Win32, 8 on arm64", "uintptr_t": "4 bytes on Win32, 8 on arm64",
                 "time_t": "time_t is 8 bytes on modern MSVC (4 with _USE_32BIT_TIME_T), 8 on macOS"}
# Windows SDK scalar typedefs. They are measured by the dumper too (the compat layer defines them),
# so a wrong compat typedef shows up in the comparison.
WIN_SCALARS = {"BYTE", "WORD", "DWORD", "UINT", "INT", "BOOL", "LONG", "ULONG", "SHORT", "USHORT", "CHAR",
               "UCHAR", "FLOAT", "DOUBLE", "TCHAR", "WCHAR", "LONGLONG", "ULONGLONG", "DWORD_PTR", "INT_PTR",
               "UINT_PTR", "LONG_PTR", "ULONG_PTR", "SIZE_T", "LPARAM", "WPARAM", "HRESULT", "COLORREF",
               "D3DCOLOR", "__time32_t", "__time64_t", "BOOLEAN", "INT64", "UINT64", "DWORD64", "LPSTR",
               "LPCSTR", "LPVOID", "PBYTE", "LPBYTE", "HANDLE", "HWND", "HMODULE", "int8_t", "int16_t",
               "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t"}
KEYWORDS = {"const", "volatile", "struct", "class", "union", "enum", "typename", "static", "mutable", "inline",
            "virtual", "explicit", "extern", "register", "friend", "__declspec", "__forceinline", "__cdecl",
            "__stdcall", "CALLBACK", "WINAPI"}
NOT_TYPE_HEAD = {"return", "sizeof", "delete", "new", "throw", "case", "goto", "else", "do", "if", "while",
                 "for", "switch", "operator", "using", "typedef", "public", "private", "protected"}
# Records that are not byte-serialisable on any platform although a sizeof of them reaches a
# file call (verified by hand); they are listed as comments with the reason.
NOT_FILE = {
}

TOKEN = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/|#(?:\\\r?\n|[^\n])*'
                   r'|[A-Za-z_]\w*|\d[\w.]*|::|->|\S', re.S)
IDENT = re.compile(r"[A-Za-z_]\w*$")


def tokenize(text):
    toks, lines = [], []
    line = 1
    pos = 0
    for m in TOKEN.finditer(text):
        line += text.count("\n", pos, m.start())
        pos = m.start()
        t = m.group(0)
        if not t.startswith(("//", "/*", "#")):
            toks.append(t)
            lines.append(line)
    return toks, lines


def read(path):
    return path.read_bytes().decode("cp949", errors="replace")


def match_close(toks, i, open_="(", close=")"):
    depth = 0
    for j in range(i, len(toks)):
        if toks[j] == open_:
            depth += 1
        elif toks[j] == close:
            depth -= 1
            if depth == 0:
                return j
    return len(toks) - 1


def split_top(toks, sep=","):
    """Split at top-level separators (outside (), [], <>)."""
    out, cur, depth = [], [], 0
    for t in toks:
        if t in "([<":
            depth += 1
        elif t in ")]>":
            depth -= 1
        if t == sep and depth == 0:
            out.append(cur)
            cur = []
        else:
            cur.append(t)
    out.append(cur)
    return out


class Decl:
    """A declared variable/member: base type tokens, pointer depth, array dims, declaring scope."""
    def __init__(self, type_toks, ptr, arrays, scope, file, line):
        self.type_toks, self.ptr, self.arrays, self.scope, self.file, self.line = type_toks, ptr, arrays, scope, file, line


def parse_declarator_list(stmt):
    """'const A::B *p, q[4] : 3 = x' -> (base type tokens, [(name, ptr, arrays)]) or None."""
    stmt = [t for t in stmt]
    while stmt and stmt[0] in ("static", "mutable", "const", "volatile", "extern", "register", "inline"):
        stmt = stmt[1:]
    if not stmt or "(" in stmt or "operator" in stmt or stmt[0] in NOT_TYPE_HEAD:
        return None
    parts = split_top(stmt)
    first = parts[0]
    # cut initialiser / bitfield
    for k, t in enumerate(first):
        if t in ("=", ":") and (k == 0 or first[k - 1] != ":"):
            if t == ":" and k + 1 < len(first) and first[k + 1] == ":":
                continue
            first = first[:k]
            break
    # name = last identifier not inside [] / <>
    depth, name_i = 0, None
    for k, t in enumerate(first):
        if t in "[<":
            depth += 1
        elif t in "]>":
            depth -= 1
        elif depth == 0 and IDENT.match(t) and t not in KEYWORDS:
            name_i = k
    if name_i is None or name_i == 0:
        return None
    base = first[:name_i]
    ptr = 0
    while base and base[-1] in ("*", "&"):
        ptr += base[-1] == "*"
        base = base[:-1]
    base = [t for t in base if t not in ("const", "volatile", "static", "mutable")]
    if not base or not (IDENT.match(base[-1]) or base[-1] == ">"):
        return None
    if base[0] in NOT_TYPE_HEAD or base[-1] in NOT_TYPE_HEAD:
        return None
    arrays = first[name_i + 1:].count("[")
    decls = [(first[name_i], ptr, arrays)]
    for p in parts[1:]:
        p2 = p
        for k, t in enumerate(p2):
            if t in ("=", ":"):
                p2 = p2[:k]
                break
        nptr = 0
        while p2 and p2[0] in ("*", "&"):
            nptr += p2[0] == "*"
            p2 = p2[1:]
        if p2 and IDENT.match(p2[0]):
            decls.append((p2[0], nptr, p2[1:].count("[")))
    return base, decls


class Index:
    def __init__(self):
        self.records = {}    # qname -> dict(kind, file, line, bases, members{name: Decl}, scope)
        self.typedefs = {}   # qname -> Decl (scope = enclosing scope of the typedef)
        self.enums = {}      # qname -> file
        self.enumerators = {}  # qualified enumerator -> enum qname
        self.cpp_types = set()
        self.template_params = {}  # file -> {names used as template parameters}
        self.local_records = {}    # (file, name) -> qname of a record defined inside a function

    def add_record(self, qname, kind, file, line, bases):
        r = self.records.get(qname)
        if r is None or (r["file"].endswith(".cpp") and not file.endswith(".cpp")):
            self.records[qname] = {"kind": kind, "file": file, "line": line, "bases": bases, "members": {},
                                   "scope": "::".join(qname.split("::")[:-1])}
            return True
        return r["file"] == file and r["line"] == line


def strip_head(header):
    """Drop access specifiers, template<...>, __declspec(...), typedef from a statement head."""
    h = list(header)
    changed = True
    typedef = False
    while changed and h:
        changed = False
        if len(h) >= 2 and h[0] in ("public", "private", "protected") and h[1] == ":":
            h, changed = h[2:], True
        elif h[0] == "template" and len(h) > 1 and h[1] == "<":
            h, changed = h[match_close(h, 1, "<", ">") + 1:], True
        elif h[0] in ("__declspec", "__pragma") and len(h) > 1 and h[1] == "(":
            h, changed = h[match_close(h, 1) + 1:], True
        elif h[0] == "typedef":
            h, changed, typedef = h[1:], True, True
        elif h[0] in ("extern",) and len(h) > 1 and h[1].startswith('"'):
            h, changed = h[2:], True
    return h, typedef


def walk(path, rel, index, sites):
    """One pass over a file: fills the index (records, members, typedefs, enums) and, when `sites`
    is a list, collects the file-IO call sites with their class context."""
    toks, lines = tokenize(read(path))
    is_cpp = not rel.lower().endswith((".h", ".hpp", ".inl"))
    if index is not None:
        tp = index.template_params.setdefault(rel, set())
        for k in range(len(toks) - 2):
            if toks[k] == "template" and toks[k + 1] == "<":
                e = match_close(toks, k + 1, "<", ">")
                for part in split_top(toks[k + 2:e]):
                    if part and part[0] in ("typename", "class") and len(part) > 1:
                        tp.add(part[1])
    # frame: dict(kind, name(qualified scope for ns/record), ctx, start, typedef, func_head)
    stack = [{"kind": "ns", "name": "", "ctx": "", "start": 0}]
    stmt_start = 0
    post_record = None   # (record qname, typedef?, frame-parent) waiting for declarators after '}'

    def scope_name():
        for f in reversed(stack):
            if f["kind"] in ("ns", "record"):
                return f["name"]
        return ""

    def record_frame():
        f = stack[-1]
        return f if f["kind"] == "record" else None

    i = 0
    n = len(toks)
    while i < n:
        t = toks[i]
        if t == "{":
            head, typedef = strip_head(toks[stmt_start:i])
            parent = stack[-1]
            frame = {"kind": "other", "name": parent.get("name", ""), "ctx": parent["ctx"], "start": i}
            if parent["kind"] in ("func", "block") and head and head[0] in ("struct", "class", "union") \
                    and len(head) > 1 and IDENT.match(head[1]) and "(" not in head and "=" not in head:
                # function-local record (TargaHeader in a loader): found by (file, name)
                func = parent.get("func", parent)
                qname = (func.get("fname") or "local") + "::" + head[1]
                frame.update(kind="record", name=qname, ctx=qname, typedef=typedef, anon=False, func=None)
                if index is not None:
                    index.add_record(qname, head[0], rel, lines[i], [])
                    index.cpp_types.add(qname)
                    index.local_records[(rel, head[1])] = qname
            elif parent["kind"] in ("func", "block"):
                frame["kind"] = "block"
                frame["func"] = parent.get("func", parent)
            elif head and head[0] == "namespace":
                nm = head[1] if len(head) > 1 and IDENT.match(head[1]) else ""
                base = scope_name()
                frame.update(kind="ns", name=(base + "::" + nm if base else nm) if nm else base)
            elif not head:
                frame["kind"] = "ns" if parent["kind"] == "ns" and not toks[stmt_start - 1:stmt_start] == ["="] else "other"
                frame["name"] = scope_name()
            elif head[0] in ("struct", "class", "union") and "(" not in head and "=" not in head:
                nm = head[1] if len(head) > 1 and IDENT.match(head[1]) and head[1] not in KEYWORDS else ""
                # struct __declspec(align(16)) X / struct X final
                k = 1
                while k < len(head) and head[k] in ("__declspec",):
                    k = match_close(head, k + 1) + 1
                if k < len(head) and IDENT.match(head[k]):
                    nm = head[k]
                bases = []
                if ":" in head:
                    for part in split_top(head[head.index(":") + 1:]):
                        part = [x for x in part if x not in ("public", "private", "protected", "virtual")]
                        if part:
                            bases.append("".join(part))
                base = scope_name()
                qname = (base + "::" + nm if base else nm) if nm else ""
                anon = not nm
                if anon:
                    qname = (base + "::" if base else "") + f"<anon@{rel}:{lines[i]}>"
                frame.update(kind="record", name=qname, ctx=qname, typedef=typedef, anon=anon)
                if index is not None:
                    index.add_record(qname, head[0], rel, lines[i], bases)
                    if is_cpp:
                        index.cpp_types.add(qname)
            elif head[0] == "enum":
                k = 1
                if k < len(head) and head[k] in ("class", "struct"):
                    k += 1
                nm = head[k] if k < len(head) and IDENT.match(head[k]) else ""
                base = scope_name()
                qname = (base + "::" + nm if base else nm) if nm else ""
                frame.update(kind="enum", name=qname, typedef=typedef)
                if index is not None and nm:
                    index.enums[qname] = rel
                if index is not None:
                    # enumerators live in the enclosing scope (unscoped enums)
                    e = match_close(toks, i, "{", "}")
                    sc = scope_name()
                    for part in split_top(toks[i + 1:e]):
                        if part and IDENT.match(part[0]):
                            index.enumerators[(sc + "::" + part[0]) if sc else part[0]] = qname or "int"
            elif "(" in head and parent["kind"] in ("ns", "record") and head[-1] != "=":
                # function definition: qualifier of the name before the first '('
                p = head.index("(")
                k = p - 1
                parts = []
                while k >= 0 and (IDENT.match(head[k]) or head[k] == "~"):
                    if head[k] != "~":
                        parts.insert(0, head[k])
                    k -= 1
                    if k >= 0 and head[k] == "::":
                        k -= 1
                        continue
                    if k >= 0 and head[k] == "~":
                        continue
                    break
                qual = "::".join(parts[:-1])
                if parent["kind"] == "record":
                    ctx = parent["name"]
                else:
                    sc = scope_name()
                    ctx = (sc + "::" + qual if sc else qual) if qual else sc
                frame.update(kind="func", ctx=ctx, head=head, fname="::".join(parts))
                frame["func"] = frame
            stack.append(frame)
            stmt_start = i + 1
        elif t == "}":
            frame = stack.pop() if len(stack) > 1 else stack[0]
            if frame["kind"] == "record":
                post_record = (frame["name"], frame.get("typedef"), i + 1)
            elif frame["kind"] == "enum":
                post_record = (frame["name"] or None, frame.get("typedef"), i + 1, "enum")
            else:
                post_record = None
            stmt_start = i + 1
            if frame["kind"] in ("func", "block", "ns", "other") or (i + 1 < n and toks[i + 1] != ";" and
                                                                      not IDENT.match(toks[i + 1]) and toks[i + 1] != "*"):
                pass
        elif t == ";":
            stmt = toks[stmt_start:i]
            fr = stack[-1]
            if post_record is not None and post_record[2] == stmt_start and index is not None:
                # declarators after a record/enum body: typedef names or member/variable names
                rq, tdef = post_record[0], post_record[1]
                for part in split_top(stmt):
                    nptr = 0
                    while part and part[0] in ("*", "&"):
                        nptr += part[0] == "*"
                        part = part[1:]
                    if part and IDENT.match(part[0]):
                        nm = part[0]
                        tt = [rq] if rq else ["int"]
                        if tdef:
                            sc = scope_name()
                            index.typedefs[(sc + "::" + nm) if sc else nm] = Decl(tt, nptr, part[1:].count("["), sc, rel, lines[i])
                        elif fr["kind"] == "record":
                            index.records.get(fr["name"], {"members": {}})["members"][nm] = Decl(
                                tt, nptr, part[1:].count("["), fr["name"], rel, lines[i])
            elif index is not None and fr["kind"] in ("record", "ns"):
                head, typedef = strip_head(stmt)
                if typedef and head:
                    if "(" in head:   # function pointer typedef
                        m = [x for x in head if IDENT.match(x)]
                        k = head.index("(")
                        if k + 2 < len(head) and head[k + 1] == "*" and IDENT.match(head[k + 2]):
                            sc = scope_name()
                            index.typedefs[(sc + "::" + head[k + 2]) if sc else head[k + 2]] = Decl(["void"], 1, 0, sc, rel, lines[i])
                    else:
                        parsed = parse_declarator_list(head)
                        if parsed:
                            base, decls = parsed
                            sc = scope_name()
                            for nm, ptr, arr in decls:
                                index.typedefs[(sc + "::" + nm) if sc else nm] = Decl(base, ptr, arr, sc, rel, lines[i])
                elif fr["kind"] == "record" and head and head[0] not in ("friend", "using", "typedef", "enum", "struct", "class", "union") \
                        or (fr["kind"] == "record" and head and head[0] in ("struct", "class", "union", "enum") and len(head) > 2):
                    parsed = parse_declarator_list(head)
                    if parsed:
                        base, decls = parsed
                        # members of anonymous unions/structs are also members of the enclosing record
                        owners = [fr["name"]]
                        k = len(stack) - 1
                        while k > 0 and stack[k]["kind"] == "record" and stack[k].get("anon"):
                            k -= 1
                            if stack[k]["kind"] == "record":
                                owners.append(stack[k]["name"])
                        for owner in owners:
                            rec = index.records.get(owner)
                            if rec is not None:
                                for nm, ptr, arr in decls:
                                    rec["members"].setdefault(nm, Decl(base, ptr, arr, fr["name"], rel, lines[i]))
            post_record = None
            stmt_start = i + 1
        elif t == ":" and i + 1 < n and toks[i - 1] in ("public", "private", "protected"):
            stmt_start = i + 1
        elif sites is not None and IDENT.match(t) and i + 1 < n and toks[i + 1] == "(" and \
                (t in IO_CALLS or t in COPY_CALLS) and (i == 0 or toks[i - 1] not in ("BOOL", "void", "size_t", "virtual")):
            j = match_close(toks, i + 1)
            fr = stack[-1]
            func = fr.get("func")
            sites.append({"file": rel, "line": lines[i], "call": t, "args": toks[i + 2:j],
                          "ctx": fr["ctx"], "func": func, "pos": i, "toks": toks})
        elif sites is not None and t == "sizeof" and i + 1 < n and toks[i + 1] == "(" and stack[-1]["kind"] in ("func", "block") \
                and (toks[i - 2:i] in (["=", "="], ["!", "="]) or toks[match_close(toks, i + 1) + 1:match_close(toks, i + 1) + 3]
                     in (["=", "="], ["!", "="])):
            # `dwSize == sizeof(T)`: a size read from the file decides how a blob is interpreted
            # (the DxEffect SetProperty(PBYTE, dwSize, dwVer) pattern, then memcpy(&m_Property, p, dwSize))
            j = match_close(toks, i + 1)
            fr = stack[-1]
            sites.append({"file": rel, "line": lines[i], "call": "size-check", "args": toks[i:j + 1],
                          "ctx": fr["ctx"], "func": fr.get("func"), "pos": i, "toks": toks})
        elif sites is not None and t == ">" and i + 2 < n and toks[i + 1] == ">" and stack[-1]["kind"] in ("func", "block") \
                or sites is not None and t == "<" and i + 2 < n and toks[i + 1] == "<" and stack[-1]["kind"] in ("func", "block"):
            # stream operator: operand up to the next ; ) , >> <<
            j = i + 2
            while j < n and toks[j] not in (";", ")", ",") and not (toks[j] in "<>" and toks[j + 1] == toks[j]):
                j += 1
            lhs = toks[i - 1] if i else ""
            if lhs not in ("<", ">") and toks[i - 2:i - 1] != ["operator"]:
                fr = stack[-1]
                sites.append({"file": rel, "line": lines[i], "call": t + t, "args": toks[i + 2:j],
                              "ctx": fr["ctx"], "func": fr.get("func"), "pos": i, "toks": toks})
            i = j
            continue
        i += 1


class Resolver:
    def __init__(self, index):
        self.ix = index
        self.cur_file = None   # file of the call site being resolved (function-local records)

    def lookup(self, name, ctx, depth=0):
        """Qualified name of a type `name` (may contain ::) seen from scope `ctx`."""
        if depth > 12:
            return None
        if name.startswith("::"):
            name = name[2:]
        scopes = []
        parts = ctx.split("::") if ctx else []
        for k in range(len(parts), -1, -1):
            scopes.append("::".join(parts[:k]))
        for sc in scopes:
            q = sc + "::" + name if sc else name
            if q in self.ix.records or q in self.ix.typedefs or q in self.ix.enums:
                return q
            # inherited nested names
            if sc in self.ix.records:
                r = self.in_bases(name, sc, depth + 1)
                if r:
                    return r
        return self.ix.local_records.get((self.cur_file, name))

    def in_bases(self, name, rec, depth):
        if depth > 12:
            return None
        for b in self.ix.records[rec]["bases"]:
            bq = self.lookup(re.sub(r"<.*", "", b), self.ix.records[rec]["scope"], depth + 1)
            if bq and bq in self.ix.records:
                q = bq + "::" + name
                if q in self.ix.records or q in self.ix.typedefs or q in self.ix.enums:
                    return q
                r = self.in_bases(name, bq, depth + 1)
                if r:
                    return r
        return None

    def enumerator(self, name, ctx):
        parts = ctx.split("::") if ctx else []
        for k in range(len(parts), -1, -1):
            sc = "::".join(parts[:k])
            q = sc + "::" + name if sc else name
            if q in self.ix.enumerators:
                return q
            if sc in self.ix.records:
                for b in self.ix.records[sc]["bases"]:
                    bq = self.lookup(re.sub(r"<.*", "", b), self.ix.records[sc]["scope"])
                    if bq and bq + "::" + name in self.ix.enumerators:
                        return bq + "::" + name
        return None

    def member(self, rec, name, depth=0):
        """Decl of member `name` in record `rec` or its bases."""
        if depth > 12 or rec not in self.ix.records:
            return None
        r = self.ix.records[rec]
        if name in r["members"]:
            return r["members"][name]
        for b in r["bases"]:
            bq = self.lookup(re.sub(r"<.*", "", b), r["scope"])
            if bq:
                d = self.member(self.typedef_target(bq), name, depth + 1)
                if d:
                    return d
        return None

    def typedef_target(self, q):
        seen = 0
        while q in self.ix.typedefs and seen < 10:
            d = self.ix.typedefs[q]
            if d.ptr or d.arrays or len(d.type_toks) != 1:
                break
            nq = self.lookup(d.type_toks[0], d.scope)
            if not nq or nq == q:
                break
            q = nq
            seen += 1
        return q

    def resolve_type(self, toks, ctx, ptr=0, arrays=0):
        """-> dict(kind, name, ptr, arrays[, args]) for a type given as tokens."""
        toks = [t for t in toks if t not in ("const", "volatile", "struct", "class", "union", "enum", "typename")]
        while toks and toks[-1] in ("*", "&"):
            ptr += toks[-1] == "*"
            toks = toks[:-1]
        if not toks:
            return {"kind": "unknown", "name": "?", "ptr": ptr, "arrays": arrays}
        if "<" in toks:
            k = toks.index("<")
            tname = "".join(toks[:k])
            args = split_top(toks[k + 1:match_close(toks, k, "<", ">")])
            return {"kind": "template", "name": tname, "ptr": ptr, "arrays": arrays,
                    "args": [self.resolve_type(a, ctx) for a in args]}
        name = "".join(toks)
        if all(t in BUILTIN or t in RISKY_BUILTIN for t in toks):
            spelled = " ".join(toks)
            risky = [t for t in toks if t in RISKY_BUILTIN]
            if risky == ["long", "long"] or toks.count("long") == 2:
                risky = []
            return {"kind": "builtin", "name": spelled, "ptr": ptr, "arrays": arrays,
                    "risky": RISKY_BUILTIN[risky[0]] if risky else None}
        if name.endswith(("size_type", "difference_type")):
            return {"kind": "builtin", "name": name, "ptr": ptr, "arrays": arrays, "risky": RISKY_BUILTIN["size_t"]}
        if name.startswith("std::") or name in ("CString", "CStringA", "CStringW"):
            return {"kind": "template", "name": name, "ptr": ptr, "arrays": arrays, "args": []}
        q = self.lookup(name, ctx)
        if q is None:
            if name in RISKY_BUILTIN:
                return {"kind": "builtin", "name": name, "ptr": ptr, "arrays": arrays, "risky": RISKY_BUILTIN[name]}
            if name in WIN_SCALARS or name.startswith(("LP", "P")) and name.upper() == name and name[1:] in WIN_SCALARS:
                return {"kind": "scalar", "name": name, "ptr": ptr, "arrays": arrays}
            return {"kind": "sdk", "name": name, "ptr": ptr, "arrays": arrays}
        if q in self.ix.typedefs:
            d = self.ix.typedefs[q]
            inner = self.resolve_type(d.type_toks, d.scope, d.ptr, d.arrays)
            if inner["kind"] in ("record", "sdk") and not inner["ptr"]:
                # keep the typedef name only if it is the public spelling of an anonymous record
                if "<anon@" in inner["name"]:
                    inner["name"] = q
            inner["ptr"] += ptr
            inner["arrays"] += arrays
            inner.setdefault("via", q)
            return inner
        if q in self.ix.enums:
            return {"kind": "enum", "name": q, "ptr": ptr, "arrays": arrays}
        return {"kind": "record", "name": q, "ptr": ptr, "arrays": arrays}

    # ---- expressions -------------------------------------------------------------------------
    def local_decl(self, site, name):
        """Find `T name` declared before the site inside the function (or in its parameter list)."""
        toks = site["toks"]
        func = site["func"]
        if not func:
            return None
        start = func["start"]
        for k in range(site["pos"] - 1, start, -1):
            if toks[k] != name:
                continue
            prev = toks[k - 1]
            if not (IDENT.match(prev) or prev in ("*", "&", ">")) or prev in NOT_TYPE_HEAD or prev in ("const",):
                continue
            if toks[k + 1] not in (";", "=", "[", ",", "(", ")", ":") :
                continue
            # statement start
            b = k - 1
            while b > start and toks[b] not in (";", "{", "}", "(", ","):
                b -= 1
            stmt = toks[b + 1:k + 1]
            stmt2 = stmt + toks[k + 1:k + 1 + (toks[k + 1:].index(";") if ";" in toks[k + 1:k + 80] else 0)]
            parsed = parse_declarator_list([t for t in stmt if t != "for"] + (toks[k + 1:k + 2] if toks[k + 1] == "[" else []))
            if parsed and parsed[1][0][0] == name:
                base, decls = parsed
                arrays = 0
                j = k + 1
                while j < len(toks) and toks[j] == "[":
                    arrays += 1
                    j = match_close(toks, j, "[", "]") + 1
                return Decl(base, decls[0][1], arrays, func["ctx"], site["file"], 0)
        # parameters
        head = func.get("head") or []
        if "(" in head:
            p = head.index("(")
            params = head[p + 1:match_close(head, p)]
            for part in split_top(params):
                part = [x for x in part]
                if "=" in part:
                    part = part[:part.index("=")]
                if part and part[-1] == name or (name in part and part[part.index(name) + 1:part.index(name) + 2] == ["["]):
                    k = part.index(name)
                    base = part[:k]
                    ptr = 0
                    while base and base[-1] in ("*", "&"):
                        ptr += base[-1] == "*"
                        base = base[:-1]
                    return Decl([x for x in base if x != "const"], ptr, part[k + 1:].count("["), func["ctx"], site["file"], 0)
        return None

    def resolve_expr(self, toks, site):
        """Type of the expression inside sizeof(...)."""
        ctx = site["ctx"]
        toks = list(toks)
        # a type (or a constant) rather than an expression?
        t2 = [t for t in toks if t not in ("const", "struct", "class", "union", "enum", "typename")]
        if t2 and IDENT.match(t2[0]) and all(IDENT.match(t) or t in ("::", "*", "<", ">", ",") for t in t2):
            core = [t for t in t2 if t != "*"]
            joined = "".join(core)
            if len(core) == 1 and self.var_type(core[0], site):
                pass   # a variable: handled below
            elif all(t in BUILTIN or t in RISKY_BUILTIN for t in core) or "<" in core:
                return self.resolve_type(t2, ctx)
            elif joined in self.ix.template_params.get(site["file"], ()):
                return {"kind": "tparam", "name": joined, "ptr": 0, "arrays": 0}
            elif self.lookup(joined, ctx):
                return self.resolve_type(t2, ctx)
            elif self.enumerator(joined, ctx):
                return {"kind": "builtin", "name": "int", "ptr": 0, "arrays": 0, "risky": None}
            elif len(core) >= 3 and core[-2] == "::":
                # Class::static_member  /  Class::nested_typedef_of_a_template (size_type)
                if core[-1] in ("size_type", "difference_type"):
                    return {"kind": "builtin", "name": core[-1], "ptr": 0, "arrays": 0,
                            "risky": RISKY_BUILTIN["size_t"]}
                owner = self.lookup("".join(core[:-2]), ctx)
                if owner:
                    d = self.member(self.typedef_target(owner), core[-1])
                    if d:
                        return self.resolve_type(d.type_toks, d.scope, d.ptr, d.arrays)
                    if self.enumerator(core[-1], self.typedef_target(owner)):
                        return {"kind": "builtin", "name": "int", "ptr": 0, "arrays": 0, "risky": None}
                return None
            elif len(core) == 1 and core[0] in ("TRUE", "FALSE"):
                return {"kind": "builtin", "name": "int", "ptr": 0, "arrays": 0, "risky": None}
            elif len(core) == 1 and (core[0] in WIN_SCALARS or core[0] in RISKY_BUILTIN or
                                     re.match(r"(D3D|DX|BITMAP|WAVE|GUID|RECT|POINT|SIZE|RGB|PALETTE)", core[0])
                                     or core[0].startswith("std")):
                return self.resolve_type(t2, ctx)
            elif len(core) == 3 and core[0] == "std":
                return self.resolve_type(t2, ctx)
            elif len(core) == 1 and not self.var_type(core[0], site):
                return None   # unknown name: reported as unresolved
        # expression: [*]* primary (.m | ->m | [..])*
        deref = 0
        while toks and toks[0] in ("*", "("):
            if toks[0] == "*":
                deref += 1
                toks = toks[1:]
            else:
                toks = toks[1:match_close(toks, 0)] + toks[match_close(toks, 0) + 1:]
        if not toks or not IDENT.match(toks[0]):
            return None
        k = 0
        if toks[0] == "this" and len(toks) > 2 and toks[1] == "->":
            cur = {"kind": "record", "name": ctx, "ptr": 0, "arrays": 0}
            k = 1
        else:
            d = self.var_type(toks[0], site)
            # Class::member
            while d is None and k + 2 < len(toks) and toks[k + 1] == "::":
                k += 2
            if d is None:
                return None
            cur = self.resolve_type(d.type_toks, d.scope, d.ptr, d.arrays)
        k += 1
        while k < len(toks):
            t = toks[k]
            if t == "[":
                e = match_close(toks, k, "[", "]")
                if cur["arrays"]:
                    cur = dict(cur, arrays=cur["arrays"] - 1)
                elif cur["ptr"]:
                    cur = dict(cur, ptr=cur["ptr"] - 1)
                elif cur["kind"] == "template":
                    cur = dict(cur["args"][0]) if cur.get("args") else cur
                k = e + 1
                continue
            if t in (".", "->") and k + 1 < len(toks):
                if cur["kind"] != "record":
                    return None
                d = self.member(cur["name"], toks[k + 1])
                if d is None:
                    return None
                cur = self.resolve_type(d.type_toks, d.scope, d.ptr, d.arrays)
                k += 2
                continue
            if t == "(":
                return None   # function call: unknown return type
            k += 1
        if deref:
            cur = dict(cur)
            if cur["arrays"]:
                cur["arrays"] -= deref
            else:
                cur["ptr"] = max(0, cur["ptr"] - deref)
        return cur

    def var_type(self, name, site):
        d = self.local_decl(site, name)
        if d:
            return d
        ctx = site["ctx"]
        parts = ctx.split("::") if ctx else []
        for k in range(len(parts), 0, -1):
            d = self.member(self.typedef_target("::".join(parts[:k])), name)
            if d:
                return d
        return None


def sizeof_args(args):
    """Every sizeof(...) operand inside a call's argument tokens."""
    out = []
    for k, t in enumerate(args):
        if t == "sizeof":
            if k + 1 < len(args) and args[k + 1] == "(":
                e = match_close(args, k + 1)
                out.append(args[k + 2:e])
            elif k + 1 < len(args):
                out.append(args[k + 1:k + 2])
    return out


def main():
    drops = set()
    if "--drop" in sys.argv:
        drops = set(sys.argv[sys.argv.index("--drop") + 1:])
    if OUT_LIST.exists():
        for line in OUT_LIST.read_text().splitlines():
            m = re.match(r"// dropped: (\S+)", line)
            if m:
                drops.add(m.group(1))

    sources = subprocess.check_output([sys.executable, str(ROOT / "port/scripts/client_sources.py")]).decode().splitlines()
    headers = []
    for d in PROJECT_DIRS:
        for ext in ("*.h", "*.hpp", "*.inl"):
            headers += sorted((ROOT / d).rglob(ext))
    headers = sorted(set(headers))
    index = Index()
    sites = []
    for h in headers:
        walk(h, str(h.relative_to(ROOT)), index, sites)
    for s in sources:
        walk(ROOT / s, s, index, sites)
    # second pass for the .cpp call sites now that every header is indexed is not needed: the
    # resolver works on the finished index; sites only carry tokens and context.
    res = Resolver(index)

    def func_does_io(site):
        f = site["func"]
        if not f:
            return False
        toks = site["toks"]
        e = match_close(toks, f["start"], "{", "}")
        body = toks[f["start"]:e]
        return any(x in IO_CALLS or x == "ReadBuffer" for x in body) or ">" in body and any(
            body[k] == ">" and body[k + 1] == ">" and IDENT.match(body[k - 1] or "") and "File" in body[k - 1]
            for k in range(1, len(body) - 1))

    def func_copies(site):
        """The function moves raw bytes (so a `size == sizeof(T)` check guards a raw copy/read)."""
        f = site["func"]
        if not f:
            return False
        toks = site["toks"]
        body = toks[f["start"]:match_close(toks, f["start"], "{", "}")]
        return any(x in IO_CALLS or x in COPY_CALLS for x in body)

    def copy_from_param(site):
        f = site["func"]
        if not f or len(site["args"]) < 3:
            return False
        parts = split_top(site["args"])
        if len(parts) < 2:
            return False
        src = [t for t in parts[1] if t not in ("(", ")")]
        if not src or not IDENT.match(src[-1]):
            return False
        d = res.local_decl(site, src[-1])
        return bool(d and d.ptr and (d.type_toks[-1:] in (["BYTE"], ["char"], ["void"]) or
                                     d.type_toks[-1:] in (["PBYTE"], ["LPBYTE"])) and d.line == 0)

    types = {}   # display name -> info
    unresolved = []
    risky_sites = []
    for s in sites:
        call = s["call"]
        res.cur_file = s["file"]
        if call in COPY_CALLS:
            if not (func_does_io(s) or copy_from_param(s)):
                continue
            via = "memcpy-from-file-buffer"
        elif call == "size-check":
            if not func_copies(s):
                continue
            # GASSERT(sizeof(x)==dwSize) before a field-by-field LOAD only asserts (no-op in Release)
            via = "assert-only" if "GASSERT" in s["toks"][max(0, s["pos"] - 8):s["pos"]] else "size-check+memcpy"
        elif call in (">>", "<<"):
            via = "stream"
        else:
            via = call
        found = []
        if call in (">>", "<<"):
            ops = s["args"]
            if not ops or not IDENT.match(ops[0]) and ops[0] not in ("*", "("):
                continue
            r = res.resolve_expr(ops, s)
            # only the std::vector<T> templates move raw bytes; every other overload is a scalar
            if r and r["kind"] == "template" and r["name"].split("::")[-1] == "vector" and not r["ptr"] \
                    and not r["arrays"] and r.get("args"):
                found.append((ops, r["args"][0], "stream-vector"))
            else:
                continue
        else:
            for expr in sizeof_args(s["args"]):
                r = res.resolve_expr(expr, s)
                if r is None:
                    unresolved.append({"file": s["file"], "line": s["line"], "call": call, "expr": " ".join(expr),
                                       "ctx": s["ctx"]})
                    continue
                found.append((expr, r, via))
        fname = s["func"]["fname"] if s["func"] else ""
        for expr, r, how in found:
            site = {"file": s["file"], "line": s["line"], "call": call, "expr": " ".join(expr), "func": fname, "via": how}
            if r["kind"] == "tparam":
                continue   # generic code (basestream vector templates); the instantiations are found at their call sites
            if r["ptr"]:
                risky_sites.append(dict(site, type=r["name"] + " " + "*" * r["ptr"], why="sizeof of a pointer: 4 bytes on Win32, 8 on arm64"))
                continue
            if r["kind"] == "builtin":
                if r.get("risky"):
                    risky_sites.append(dict(site, type=r["name"], why=r["risky"]))
                continue
            if r["kind"] == "template":
                risky_sites.append(dict(site, type=r["name"], why="template/library type read as raw bytes"))
                continue
            if r["kind"] == "unknown":
                unresolved.append(dict(site, ctx=s["ctx"]))
                continue
            name = r["name"]
            info = types.setdefault(name, {"kind": r["kind"], "sites": [], "via_typedef": set()})
            if r.get("via"):
                info["via_typedef"].add(r["via"])
            info["sites"].append(site)

    # header + exclusion per type
    listed, excluded = [], {}
    for name, info in sorted(types.items()):
        kind = info["kind"]
        if kind == "record":
            rec = index.records[name]
            info["header"] = rec["file"]
            info["decl_line"] = rec["line"]
            info["record_kind"] = rec["kind"]
            if "<anon@" in name:
                excluded[name] = "anonymous record without a typedef name"
            elif name in index.cpp_types and rec["file"].endswith(".cpp"):
                excluded[name] = f"defined in {rec['file']} (not includable); layout checked by analyze_file_structs.py"
            elif name in NOT_FILE:
                excluded[name] = NOT_FILE[name]
            else:
                listed.append(name)
        elif kind == "enum":
            info["header"] = index.enums[name]
            excluded[name] = "enum: int-sized on MSVC and clang alike (no fixed underlying type)"
        elif kind == "scalar":
            info["header"] = "(Windows SDK / compat)"
            listed.append(name)
        else:   # sdk record (D3DX, Windows)
            info["header"] = "(Windows SDK / D3DX via framework.h)"
            listed.append(name)
    for d in drops:
        if d in types and d not in excluded:
            excluded[d] = "dropped (does not compile in the dumper)"
    listed = [n for n in listed if n not in excluded]

    hdrs = PRELUDE + sorted({types[n]["header"] for n in listed if types[n]["kind"] == "record"} - set(PRELUDE))
    GOLDEN.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated by port/scripts/gen_file_struct_sizes.py - do not edit.",
             "// Types read from / written to files as raw bytes; sizes must match Windows x86."]
    lines += [f"// dropped: {d}" for d in sorted(drops)]
    lines += [f"// not measured: {n} - {why}" for n, why in sorted(excluded.items()) if n not in drops]
    lines += [f"RAN_FSIZE({n})" for n in listed]
    OUT_LIST.write_text("\n".join(lines) + "\n")
    OUT_HDRS.write_text("// Generated by port/scripts/gen_file_struct_sizes.py - do not edit.\n" +
                        "".join(f'#include "../../../{h}"\n' for h in hdrs))
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    for info in types.values():
        info["via_typedef"] = sorted(info["via_typedef"])
    OUT_JSON.write_text(json.dumps({"types": types, "excluded": excluded, "listed": listed,
                                    "risky_sites": risky_sites, "unresolved": unresolved}, indent=1))
    print(f"{len(sites)} call sites, {len(types)} types ({len(listed)} measured, {len(excluded)} excluded), "
          f"{len(risky_sites)} risky scalar/pointer sites, {len(unresolved)} unresolved sizeof operands")
    print(f"-> {OUT_LIST.relative_to(ROOT)}, {OUT_HDRS.relative_to(ROOT)}, {OUT_JSON.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
