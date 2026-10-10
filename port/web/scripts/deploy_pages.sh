#!/bin/bash
# port/web/scripts/deploy_pages.sh [--data] - publishes the browser client on Cloudflare Pages
# (free plan: no bandwidth or request billing), project "ran-odyssey" -> https://ran-odyssey.pages.dev
#
# The page, the client (ran.js + ran.wasm) and the game files (port/web/scripts/package_files.py,
# under data/) are one static site; wrangler uploads only files Pages does not have yet, so an
# update sends just the changed game files. --data repacks the game files first. The game
# servers are reached through the relay on api.kaleidoscopical.com (main-server routes/ran-web.js).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
B="$ROOT/port/build/web"
PROJECT="${RAN_PAGES_PROJECT:-ran-odyssey}"
cd "$ROOT"

echo "== build"
port/web/scripts/build_web.sh | tail -1
if [ "${1:-}" = "--data" ] || [ ! -f "$B/site/manifest.json" ]; then
  echo "== pack game files"
  python3 port/web/scripts/package_files.py "${RAN_CLIENT:-$HOME/Projects/RAN/client}" "$B/site"
fi

echo "== stage"
rm -rf "$B/deploy"
mkdir -p "$B/deploy"
cp port/web/shell/index.html port/web/shell/loader.js port/web/shell/login-bg.jpg port/web/shell/ui-*.png port/web/shell/_headers "$B/deploy/"
cp "$B/out/ran.js" "$B/out/ran.wasm" "$B/deploy/"
cp -Rc "$B/site" "$B/deploy/data" 2>/dev/null || cp -R "$B/site" "$B/deploy/data"   # APFS clone: no extra disk

wrangler pages project list 2>/dev/null | grep -q "$PROJECT" ||
  wrangler pages project create "$PROJECT" --production-branch main
echo "== upload"
wrangler pages deploy "$B/deploy" --project-name "$PROJECT" --branch main --commit-dirty=true
echo "published: https://$PROJECT.pages.dev/"
