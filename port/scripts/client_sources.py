#!/usr/bin/env python3
"""Prints the native client's source files, one per line, repo-relative: exactly the
<ClCompile> entries of the client .vcxproj files (so files compiled from outside a project
folder, and odd extensions like SHA.CPP, are included) minus port/native-excludes.txt.

  client_sources.py                 -> path
  client_sources.py --with-project  -> project<TAB>path

Projects: the [Client]__Game references plus [Lib]__ZLib and [Lib]__NetServer, which the
Windows client links as static libraries (only referenced objects end up in the exe).
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECTS = ["[Client]__Game", "[Lib]__Engine", "[Lib]__EngineSound", "[Lib]__EngineUI", "[Lib]__MfcEx",
            "[Lib]__NetClient", "[Lib]__RanClient", "[Lib]__RanClientUI", "[Lib]__ZLib", "[Lib]__NetServer"]


def main():
    excl = {l.strip() for l in (ROOT / "port/native-excludes.txt").read_text().splitlines()
            if l.strip() and not l.startswith("#")}
    with_project = "--with-project" in sys.argv
    for p in PROJECTS:
        vcx = sorted((ROOT / p).glob("*.vcxproj"))
        if not vcx:
            continue
        text = vcx[0].read_bytes().decode("utf-8", errors="replace")
        for rel in re.findall(r'<ClCompile Include="([^"]+)"', text):
            f = os.path.normpath(os.path.join(p, rel.replace("\\", "/")))
            if os.path.basename(f).lower() in ("pch.cpp", "stdafx.cpp") or f in excl:
                continue
            print(f"{p}\t{f}" if with_project else f)


if __name__ == "__main__":
    main()
