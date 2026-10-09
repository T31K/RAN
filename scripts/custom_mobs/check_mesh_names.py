"""Check that every mesh in a text .x skin gets a name the way the engine names it.

Mirrors DxSkinMesh9::SetupNameOnMeshContainer ([Lib]__Engine/Sources/DxMeshs/DxSkinMeshLoad9.cpp)
and SFrame::FindParentFrame (DxSkinMesh9.h): a frame holding a Mesh with no name of its own is
named "<parent>[Mesh]", but the parent is only searched DOWNWARD from the first top-level frame.
A mesh frame whose parent is a top-level sibling of that root stays unnamed, and
DxSkinMesh9::FindMeshContainer then strcmp()s a NULL name -> client crash on spawn.

  python3 check_mesh_names.py file.x [...]      exit 1 if any mesh would stay unnamed
"""
import re
import sys

TOKEN = re.compile(r'[A-Za-z_][\w\[\].\-]*|\{|\}')


class Frame:
    def __init__(self, name, parent):
        self.name, self.parent, self.children, self.has_mesh = name, parent, [], False


def parse(path):
    text = open(path, encoding='latin-1').read()
    text = re.sub(r'template\s+\w+\s*\{.*?\n\}', '', text, flags=re.S)   # templates hold braces too
    tops, stack, toks = [], [], TOKEN.findall(text)
    i = 0
    while i < len(toks):
        t = toks[i]
        if t == 'Frame':
            name = ''
            if i + 1 < len(toks) and toks[i + 1] != '{':
                name, i = toks[i + 1], i + 1
            f = Frame(name, stack[-1] if stack and isinstance(stack[-1], Frame) else None)
            (f.parent.children if f.parent else tops).append(f)
            stack.append(f)
            i += 2                                    # skip the '{'
            continue
        if t == '{':
            stack.append(None)
        elif t == '}':
            if stack:
                stack.pop()
        elif t == 'Mesh' and stack and isinstance(stack[-1], Frame):
            stack[-1].has_mesh = True
        i += 1
    return tops


def engine_parent(root, target):
    """SFrame::FindParentFrame from the first top-level frame: children only, recursively."""
    for child in root.children:
        if child is target:
            return root
        found = engine_parent(child, target)
        if found:
            return found
    return None


def walk(frames):
    for f in frames:
        yield f
        yield from walk(f.children)


def check(path):
    tops = parse(path)
    bad = []
    for f in walk(tops):
        if f.has_mesh and not f.name:
            parent = engine_parent(tops[0], f) if tops else None
            if not parent or not parent.name:
                bad.append(f.parent.name if f.parent else '<top level>')
    return bad


if __name__ == '__main__':
    failed = 0
    for p in sys.argv[1:]:
        bad = check(p)
        print(f'{"FAIL" if bad else "ok  "} {p}' + (f'  unnamed mesh under: {", ".join(bad)}' if bad else ''))
        failed |= bool(bad)
    sys.exit(1 if failed else 0)
