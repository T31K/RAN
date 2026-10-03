#!/bin/bash
# port/scripts/cmdtab_check.sh <probe-binary> <mode>
# Switches focus away and back twice (as Cmd+Tab does), then checks the window
# frame never changed and focus came back each time. Needs Terminal to have
# Accessibility permission (System Settings → Privacy & Security → Accessibility).
set -euo pipefail
BIN="$1"; MODE="${2:-desktop-fullscreen}"
LOG="$(mktemp)"
"$BIN" --mode "$MODE" --seconds 14 > "$LOG" &
PID=$!
sleep 3
for _ in 1 2; do
  osascript -e 'tell application "Finder" to activate'
  sleep 2
  osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $PID) to true"
  sleep 2
done
wait "$PID"
FIRST="$(grep '^FRAME' "$LOG" | head -1)"
LAST="$(grep '^FRAME' "$LOG" | tail -1)"
DISTINCT="$(grep '^FRAME' "$LOG" | sort -u | wc -l | tr -d ' ')"
GAINED="$(grep -c '^FOCUS gained' "$LOG" || true)"
if [ "$FIRST" = "$LAST" ] && [ "$DISTINCT" = "1" ] && [ "$GAINED" -ge 2 ]; then
  echo "PASS mode=$MODE frame=[$FIRST] focus_gained=$GAINED"
else
  echo "FAIL mode=$MODE first=[$FIRST] last=[$LAST] distinct_frames=$DISTINCT focus_gained=$GAINED"
  cat "$LOG"
  exit 1
fi
