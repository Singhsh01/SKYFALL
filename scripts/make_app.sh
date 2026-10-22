#!/usr/bin/env bash
# Build SKYFALL once in Release and install it as a normal Mac application with a Desktop
# shortcut, so the game starts with a double-click (no VS Code, no Terminal, no rebuild).
#
#   ./scripts/make_app.sh                 build, create SKYFALL.app in ~/Applications, Desktop alias
#   ./scripts/make_app.sh --out dist      only create dist/SKYFALL.app (CI / packaging)
#   ./scripts/make_app.sh --no-build      reuse the existing Release build in build/
#
# Run it again after pulling a new version: the app is replaced, the Desktop alias keeps working
# (it points at ~/Applications/SKYFALL.app), and settings, progress and downloaded maps are kept
# (they live in ~/Library/Application Support/SKYFALL, outside the app).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
OUT="$HOME/Applications"
INSTALL=1
BUILD=1
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; INSTALL=0; shift 2 ;;
    --no-build) BUILD=0; shift ;;
    *) echo "unknown option $1"; exit 1 ;;
  esac
done

if [ "$BUILD" = 1 ]; then
  if [ ! -f build/CMakeCache.txt ] || ! grep -q "CMAKE_BUILD_TYPE:STRING=Release" build/CMakeCache.txt; then
    ./scripts/build_mac.sh --configure-only
  fi
  cmake --build build --target skyfall --parallel 2   # 2 jobs keeps memory use low on 8 GB machines
fi
[ -x build/skyfall ] || { echo "build/skyfall is missing - run without --no-build"; exit 1; }

APP="$OUT/SKYFALL.app"
VERSION="$(git describe --tags --always 2>/dev/null || echo dev)"
echo "Creating $APP ($VERSION)"
mkdir -p "$OUT"
STAGE="$(mktemp -d)/SKYFALL.app"
mkdir -p "$STAGE/Contents/MacOS" "$STAGE/Contents/Resources/assets"
cp build/skyfall "$STAGE/Contents/MacOS/skyfall"
cp packaging/icon/SKYFALL.icns "$STAGE/Contents/Resources/SKYFALL.icns"
# real-terrain maps (installed into the persistent map store on first use) and the voice pack
mkdir -p "$STAGE/Contents/Resources/assets/terrain"
cp assets/terrain/*.skt assets/terrain/SHA256SUMS assets/terrain/ATTRIBUTION.md "$STAGE/Contents/Resources/assets/terrain/" 2>/dev/null || true
[ -d assets/voice ] && cp -R assets/voice "$STAGE/Contents/Resources/assets/voice"
cat > "$STAGE/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>SKYFALL</string>
  <key>CFBundleDisplayName</key><string>SKYFALL</string>
  <key>CFBundleIdentifier</key><string>io.github.singhsh01.skyfall</string>
  <key>CFBundleExecutable</key><string>skyfall</string>
  <key>CFBundleIconFile</key><string>SKYFALL</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.simulation-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
  <key>NSHumanReadableCopyright</key><string>SKYFALL - original work, MIT licence</string>
</dict>
</plist>
PLIST
# ad-hoc signature: Apple Silicon only runs signed code; a locally built app needs no notarisation
codesign --force --deep --sign - "$STAGE" >/dev/null 2>&1 || echo "  (codesign not available - the app may need right-click > Open once)"
rm -rf "$APP"
mv "$STAGE" "$APP"
touch "$APP"   # refresh the Finder / Dock icon cache

if [ "$INSTALL" = 1 ]; then
  # a real Finder alias on the Desktop: survives restarts and app updates (it follows the app)
  rm -f "$HOME/Desktop/SKYFALL" 2>/dev/null || true
  osascript >/dev/null <<OSA || ln -sf "$APP" "$HOME/Desktop/SKYFALL"
tell application "Finder"
  set a to make new alias file at (POSIX file "$HOME/Desktop" as alias) to (POSIX file "$APP" as alias)
  set name of a to "SKYFALL"
end tell
OSA
  # register with Launch Services so Spotlight and the Dock find it straight away
  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP" >/dev/null 2>&1 || true
  echo
  echo "Installed: $APP"
  echo "Desktop shortcut: ~/Desktop/SKYFALL  (double-click to play)"
  echo "Also in Spotlight (Cmd+Space, type SKYFALL) and Launchpad."
fi
