#!/bin/bash
# port/scripts/release_native.sh <version> - ship a native-app release to every player.
#
#   1. build the client, package it with the game data (~/Projects/RAN/client), Developer ID
#      signed, as a notarized + stapled DMG (the first-install download on t31k.com/ran)
#   2. notarize + staple the .app itself and zip it (what Sparkle installs)
#   3. generate_appcast: EdDSA-signed appcast.xml + delta updates from the earlier releases kept
#      in port/build/releases (KEEP that folder - deltas make updates a few MB instead of ~850 MB)
#   4. upload: zip + deltas + appcast.xml to the rolling GitHub release "native-updates" (the
#      apps' SUFeedURL), and the DMG to "installer" (the download page's link)
# Installed apps check hourly, download in the background and update when the game quits.
#
# Needs: the Developer ID cert + notary profile "ran-notary" (as package-app.command), the
# Sparkle EdDSA key in the login keychain (backup: ~/.config/ran/sparkle-ed25519-private.key),
# gh logged in to T31K/RAN.
set -euo pipefail
VERSION="${1:?usage: release_native.sh <version, e.g. 0.3>}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REPO="T31K/RAN"
FEED_TAG="native-updates"
CLIENT="${RAN_CLIENT:-$HOME/Projects/RAN/client}"
REL="$ROOT/port/build/releases"
APP="$ROOT/port/build/RanOdyssey Native.app"
DMG="$ROOT/port/build/RanOdyssey-Native.dmg"
SPARKLE_BIN="$ROOT/port/third_party/sparkle/bin"
mkdir -p "$REL"
[ -d "$SPARKLE_BIN" ] || "$ROOT/port/scripts/fetch_sparkle.sh"

[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=no -- port '[Lib]__*' '[Client]__Game' scripts)" ] || {
    echo "uncommitted source changes - commit first (the build number is the commit count)"; exit 1; }

echo "== build"
"$ROOT/port/scripts/build_native.sh" | tail -2
echo "== package + DMG (notarized)"
RAN_VERSION="$VERSION" "$ROOT/port/scripts/package_native_app.sh" --with-game "$CLIENT" --sign --dmg --notarize
BUILD="$(plutil -extract CFBundleVersion raw "$APP/Contents/Info.plist")"
ZIP="$REL/RanOdyssey-Native-$VERSION-$BUILD.zip"

echo "== notarize the app for Sparkle"
TMPZIP="$(mktemp -d)/app.zip"
ditto -c -k --keepParent "$APP" "$TMPZIP"
xcrun notarytool submit "$TMPZIP" --keychain-profile ran-notary --wait
xcrun stapler staple "$APP"
xcrun stapler validate "$APP"
rm -f "$TMPZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
echo "archive: $ZIP ($(du -h "$ZIP" | cut -f1))"

echo "== appcast + deltas"
"$SPARKLE_BIN/generate_appcast" --download-url-prefix "https://github.com/$REPO/releases/download/$FEED_TAG/" \
    --maximum-deltas 3 "$REL"
grep -o 'sparkle:version="[0-9]*"' "$REL/appcast.xml" | head -5

echo "== upload"
if ! gh release view "$FEED_TAG" -R "$REPO" >/dev/null 2>&1; then
    gh release create "$FEED_TAG" -R "$REPO" --prerelease -t "RanOdyssey Native - automatic updates" \
        -n "Update feed for the native macOS app (Sparkle). Players install from t31k.com/ran; this release is read by the app."
fi
UPLOAD=("$REL/appcast.xml" "$ZIP")
for d in "$REL"/*.delta; do [ -e "$d" ] && UPLOAD+=("$d"); done
gh release upload "$FEED_TAG" -R "$REPO" --clobber "${UPLOAD[@]}"
gh release upload installer -R "$REPO" --clobber "$DMG"
echo "released $VERSION (build $BUILD): feed https://github.com/$REPO/releases/download/$FEED_TAG/appcast.xml"
