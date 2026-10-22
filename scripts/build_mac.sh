#!/usr/bin/env bash
# Configure, build, test and launch SKYFALL on macOS (Apple Clang + CMake + raylib).
#
# If the Command Line Tools ship an SDK that is newer than their own linker understands
# (symptom: "ld: tapi error ... unknown architecture arm64e..."), this script retries with the
# newest older SDK that works. The real fix is updating the Command Line Tools:
#   softwareupdate --list   (install the newest "Command Line Tools for Xcode")
#
#   ./scripts/build_mac.sh                  configure (once), build, test, launch
#   ./scripts/build_mac.sh --configure-only configure the Release build only (used by make_app.sh)
set -uo pipefail
cd "$(dirname "$0")/.."
command -v cmake >/dev/null || { echo "cmake missing: brew install cmake"; exit 1; }
CONFIGURE_ONLY=0
if [ "${1:-}" = "--configure-only" ]; then CONFIGURE_ONLY=1; shift; fi

configure() { rm -rf build; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "$@" >build_config.log 2>&1; }

if [ -f build/CMakeCache.txt ] && [ -f build/Makefile ] && grep -q "CMAKE_BUILD_TYPE:STRING=Release" build/CMakeCache.txt; then
  echo "Reusing existing build directory (delete build/ to reconfigure)."
elif configure; then
  echo "Configured with the default SDK."
else
  echo "Default SDK failed to link a test program - trying other installed SDKs..."
  ok=0
  for sdk in $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk 2>/dev/null | sort -t X -k 2 -V -r) \
             $(ls -d /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX[0-9]*.sdk 2>/dev/null); do
    echo "  trying $sdk"
    if configure -DCMAKE_OSX_SYSROOT="$sdk"; then echo "Configured with $sdk"; ok=1; break; fi
  done
  if [ $ok -eq 0 ]; then
    echo "Could not configure. See build_config.log. Update the Command Line Tools (softwareupdate --list)."
    exit 1
  fi
fi
[ "$CONFIGURE_ONLY" = 1 ] && exit 0
set -e
cmake --build build --parallel 2          # 2 jobs keeps memory use low on 8 GB machines
ctest --test-dir build --output-on-failure
exec ./build/skyfall "$@"
