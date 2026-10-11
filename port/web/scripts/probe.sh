#!/bin/bash
# port/web/scripts/probe.sh - syntax-check every client source for the browser build and group
# the first error of each failing file. Writes port/build/web/probe.txt.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"
source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
OUT="$ROOT/port/build/web"; mkdir -p "$OUT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
python3 port/scripts/client_sources.py > "$TMP/files"
[ -n "${1:-}" ] && grep -F "$1" "$TMP/files" > "$TMP/f2" && mv "$TMP/f2" "$TMP/files"
export TMP ROOT
tr '\n' '\0' < "$TMP/files" | xargs -0 -P 10 -I{} bash -c '
  f="$1"; e=$(mktemp)
  if "$ROOT/port/web/scripts/compile_one.sh" "$f" 3 >/dev/null 2>"$e"; then
    echo "OK	$f"
  else
    loc=$(grep -a -m1 "error:" "$e")
    echo "FAIL	$f	$loc"
  fi
  rm -f "$e"
' _ {} > "$OUT/probe.txt"
echo "ok $(grep -c ^OK "$OUT/probe.txt") / $(wc -l < "$OUT/probe.txt")"
grep ^FAIL "$OUT/probe.txt" | cut -f3 | sed -E 's/^[^ ]*:[0-9]+:[0-9]+: //' | sort | uniq -c | sort -rn | head -40
