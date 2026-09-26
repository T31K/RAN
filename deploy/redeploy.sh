#!/bin/bash
# Redeploy freshly-built server exes to the VPS and restart the container.
# Usage: deploy/redeploy.sh <github-run-id>
set -euo pipefail
RUN_ID="${1:?need CI run id}"
REPO=T31K/RAN
VPS=root@167.233.23.97
DEST=/opt/ran/game-client
TMP=$(mktemp -d)

echo "[redeploy] downloading ran-binaries from run $RUN_ID …"
gh run download "$RUN_ID" --repo "$REPO" -n ran-binaries -D "$TMP"

# Locate the 4 server exes in the artifact (paths vary; find them)
for exe in SessionServer LoginServer FieldServer AgentServer; do
  src=$(find "$TMP" -iname "$exe.exe" | head -1)
  [ -z "$src" ] && { echo "[redeploy] MISSING $exe.exe in artifact"; exit 1; }
  echo "[redeploy] scp $exe.exe ($(stat -f%z "$src" 2>/dev/null || stat -c%s "$src") bytes)"
  scp -o StrictHostKeyChecking=no "$src" "$VPS:$DEST/$exe.exe"
done

echo "[redeploy] restarting the Coolify server container …"
# Coolify names the container server-<app-uuid>-<deploy-id>; match on the app uuid.
ssh -o StrictHostKeyChecking=no "$VPS" 'C=$(docker ps -q --filter name=server-giwbhnjmamk06qomgpvz02rj | head -1); [ -n "$C" ] && docker restart "$C" >/dev/null && echo restarted'
rm -rf "$TMP"
echo "[redeploy] done. tailing bind status …"
