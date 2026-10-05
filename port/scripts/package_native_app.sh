#!/bin/bash
# port/scripts/package_native_app.sh - bundle the native client into a self-contained .app.
#   port/scripts/package_native_app.sh [--with-game <client-folder>] [--sign] [--dmg] [--notarize]
#     --sign      Developer ID + hardened runtime (identity below) instead of ad hoc
#     --dmg       also build port/build/RanOdyssey-Native.dmg (signed with --sign)
#     --notarize  submit the DMG to Apple's notary service and staple it (keychain profile
#                 "ran-notary", as package-app.command); needs --sign --dmg
# Produces port/build/RanOdyssey Native.app:
#   Contents/MacOS/ran_client          the native client (port/scripts/build_native.sh)
#   Contents/Frameworks/*.dylib        DXVK d3d9, SDL3, the Vulkan loader, the KosmicKrisp
#                                      Vulkan driver and every non-system library they need,
#                                      relinked to @rpath (= Contents/Frameworks)
#   Contents/Resources/vulkan/icd.d    the driver manifest, pointing inside the bundle
#   Contents/Resources/game            only with --with-game (else the client uses the RanOdyssey
#                                      app's per-user copy, see ConfigureFromBundle in main_sdl.cpp)
# The bundle is signed ad hoc (runs on this Mac). Developer ID signing + notarisation for other
# Macs is a separate step (the existing package-app.command flow).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/port/build/RanOdyssey Native.app"
BIN="$ROOT/port/build/native/ran_client"
GAME=""; SIGN=0; DMG=0; NOTARIZE=0
IDENTITY="Developer ID Application: Teik Mun Wong (QGQYJRMCNQ)"
PROFILE="ran-notary"
while [ $# -gt 0 ]; do
    case "$1" in
        --with-game) GAME="${2:?client folder}"; shift ;;
        --sign) SIGN=1 ;;
        --dmg) DMG=1 ;;
        --notarize) NOTARIZE=1 ;;
        *) echo "unknown option $1"; exit 1 ;;
    esac
    shift
done
[ -x "$BIN" ] || { echo "build first: port/scripts/build_native.sh"; exit 1; }

MESA="$(brew --prefix mesa)"
DRIVER="$MESA/lib/libvulkan_kosmickrisp.dylib"
LOADER="$(brew --prefix vulkan-loader)/lib/libvulkan.1.dylib"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources/vulkan/icd.d"
cp "$BIN" "$APP/Contents/MacOS/ran_client"
FW="$APP/Contents/Frameworks"

# Non-system dependencies of one Mach-O file (Homebrew / our builds; system libraries stay).
deps() { otool -L "$1" | tail -n +2 | awk '{print $1}' | grep -E '^(/opt/homebrew|/usr/local|'"$ROOT"'|@rpath)' || true; }

# Resolve a dependency path to the real file (follow @rpath via the Homebrew/our lib dirs).
resolve() {
    case "$1" in
        @rpath/*) local leaf="${1#@rpath/}"
                  for d in "$ROOT/port/third_party/dxvk-install/lib" "$(brew --prefix sdl3)/lib" "$MESA/lib" /opt/homebrew/lib; do
                      [ -e "$d/$leaf" ] && { echo "$d/$leaf"; return; }
                  done; echo "" ;;
        *) echo "$1" ;;
    esac
}

# Copy every dependency (recursively) into Frameworks.
queue=("$APP/Contents/MacOS/ran_client" "$DRIVER" "$LOADER")
declare -a done_list=()
copied() { local x; for x in "${done_list[@]+"${done_list[@]}"}"; do [ "$x" = "$1" ] && return 0; done; return 1; }
while [ ${#queue[@]} -gt 0 ]; do
    f="${queue[0]}"; queue=("${queue[@]:1}")
    src="$(resolve "$f")"
    [ -n "$src" ] || { echo "cannot resolve $f"; exit 1; }
    leaf="$(basename "$src")"
    if [ "$src" != "$APP/Contents/MacOS/ran_client" ]; then
        copied "$leaf" && continue
        done_list+=("$leaf")
        cp -L "$src" "$FW/$leaf"
        chmod u+w "$FW/$leaf"
        install_name_tool -id "@rpath/$leaf" "$FW/$leaf" 2>/dev/null
        target="$FW/$leaf"
    else
        target="$src"
    fi
    for d in $(deps "$target"); do
        dl="$(basename "$(resolve "$d")")"
        install_name_tool -change "$d" "@rpath/$dl" "$target" 2>/dev/null
        queue+=("$d")
    done
done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP/Contents/MacOS/ran_client" 2>/dev/null || true
for f in "$FW"/*.dylib; do install_name_tool -add_rpath "@loader_path" "$f" 2>/dev/null || true; done

# Driver manifest: the loader resolves a relative library_path against the manifest's folder.
cat > "$APP/Contents/Resources/vulkan/icd.d/kosmickrisp_icd.json" <<EOF
{
    "ICD": {
        "api_version": "$(python3 -c "import json;print(json.load(open('$MESA/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json'))['ICD']['api_version'])")",
        "library_path": "../../../Frameworks/libvulkan_kosmickrisp.dylib"
    },
    "file_format_version": "1.0.1"
}
EOF

ICON=""
for c in /Applications/RanOdyssey.app/Contents/Resources/AppIcon.icns "$ROOT/dist/RanOdyssey.app/Contents/Resources/AppIcon.icns" \
         "$ROOT/../dist/RanOdyssey.app/Contents/Resources/AppIcon.icns"; do
    [ -f "$c" ] && { cp "$c" "$APP/Contents/Resources/AppIcon.icns"; ICON="AppIcon"; break; }
done
[ -n "$ICON" ] || { echo "no AppIcon.icns found (looked for the RanOdyssey app's icon)"; exit 1; }
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>RanOdyssey Native</string>
    <key>CFBundleDisplayName</key><string>RanOdyssey Native</string>
    <key>CFBundleIdentifier</key><string>com.t31k.ranodyssey.native</string>
    <key>CFBundleExecutable</key><string>ran_client</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.2</string>
    <key>CFBundleVersion</key><string>$(git -C "$ROOT" rev-list --count HEAD)</string>
    <key>CFBundleIconFile</key><string>$ICON</string>
    <key>LSMinimumSystemVersion</key><string>26.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>LSApplicationCategoryType</key><string>public.app-category.role-playing-games</string>
</dict>
</plist>
EOF

if [ -n "$GAME" ]; then
    echo "copying game data from $GAME ..."
    # Windows-only files stay out (executables, DLLs, debug symbols, anti-cheat, installer
    # leftovers, backups); the first launch clones this folder into Application Support.
    rsync -a --exclude 'Game.pdb' --exclude '*.exe' --exclude '*.dll' --exclude 'GameGuard' \
        --exclude '$PLUGINSDIR' --exclude '$SYSDIR' --exclude '*.bak' --exclude '*.des' \
        --exclude '.DS_Store' "$GAME/" "$APP/Contents/Resources/game/"
    # Content stamp: an updated app re-clones data/textures/sounds into existing players' copies
    # when this differs from theirs (game_sync.cpp), so new items and models reach everyone.
    ( cd "$APP/Contents/Resources/game" && find data textures sounds -type f -print0 2>/dev/null | sort -z \
        | xargs -0 shasum -a 256 | shasum -a 256 | cut -d' ' -f1 ) > "$APP/Contents/Resources/game/.data-version"
    echo "game data stamp: $(cat "$APP/Contents/Resources/game/.data-version")"
fi

if [ "$SIGN" = 1 ]; then
    # Inside out: every library, then the executable, then the bundle seal.
    for f in "$FW"/*.dylib; do
        codesign --force --timestamp --options runtime -s "$IDENTITY" "$f" >/dev/null || { echo "signing $f failed"; exit 1; }
    done
    codesign --force --timestamp --options runtime -s "$IDENTITY" "$APP/Contents/MacOS/ran_client" >/dev/null
    codesign --force --timestamp --options runtime -s "$IDENTITY" "$APP" >/dev/null
    codesign --verify --deep --strict "$APP" && echo "signed (Developer ID, hardened runtime)"
else
    codesign --force --deep --sign - "$APP" >/dev/null 2>&1 && echo "signed (ad hoc)"
fi
echo "bundle: $APP"
echo "libraries:"; ls "$FW"

if [ "$DMG" = 1 ]; then
    OUT="$ROOT/port/build/RanOdyssey-Native.dmg"
    STAGE="$(mktemp -d)"
    cp -Rc "$APP" "$STAGE/"   # APFS clone: the 2 GB bundle takes no extra disk space
    ln -s /Applications "$STAGE/Applications"
    hdiutil create -volname "RanOdyssey Native" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$OUT" >/dev/null
    rm -rf "$STAGE"
    [ "$SIGN" = 1 ] && codesign --force --timestamp -s "$IDENTITY" "$OUT"
    echo "dmg: $OUT ($(du -h "$OUT" | cut -f1))"
    if [ "$NOTARIZE" = 1 ]; then
        [ "$SIGN" = 1 ] || { echo "--notarize needs --sign"; exit 1; }
        xcrun notarytool submit "$OUT" --keychain-profile "$PROFILE" --wait
        xcrun stapler staple "$OUT" && xcrun stapler validate "$OUT"
    fi
fi
