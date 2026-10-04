#!/bin/bash
# port/scripts/package_native_app.sh - bundle the native client into a self-contained .app.
#   port/scripts/package_native_app.sh [--with-game <client-folder>]
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
GAME=""
[ "${1:-}" = "--with-game" ] && GAME="${2:?client folder}"
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
for c in /Applications/RanOdyssey.app/Contents/Resources/AppIcon.icns "$ROOT/dist/RanOdyssey.app/Contents/Resources/AppIcon.icns"; do
    [ -f "$c" ] && { cp "$c" "$APP/Contents/Resources/AppIcon.icns"; ICON="AppIcon"; break; }
done
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
    <key>CFBundleShortVersionString</key><string>0.1</string>
    <key>CFBundleVersion</key><string>$(git -C "$ROOT" rev-list --count HEAD)</string>
    <key>CFBundleIconFile</key><string>$ICON</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>LSApplicationCategoryType</key><string>public.app-category.role-playing-games</string>
</dict>
</plist>
EOF

if [ -n "$GAME" ]; then
    echo "copying game data from $GAME ..."
    rsync -a --exclude 'Game.pdb' --exclude '*.exe' --exclude '*.dll' "$GAME/" "$APP/Contents/Resources/game/"
fi

codesign --force --deep --sign - "$APP" >/dev/null 2>&1 && echo "signed (ad hoc)"
echo "bundle: $APP"
echo "libraries:"; ls "$FW"
