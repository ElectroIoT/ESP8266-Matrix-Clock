#!/usr/bin/env bash
# Publish firmware for the clocks in two steps, so a bad build never reaches them:
#
#   tools/release.sh "What changed"   build + publish as a PRE-release.
#                                      Clocks ignore pre-releases. Test release/firmware.bin
#                                      on your own clock first (web page -> Upload .bin).
#   tools/release.sh --promote        mark that version as the latest release.
#                                      Every clock installs it the following night.
#
# Before: bump FW_VERSION in src/config.h, run tools/sync_arduino.sh, commit + push.
set -euo pipefail
cd "$(dirname "$0")/.."

# the release version (the CRASH_TEST line above it is a test build's)
VER=$(sed -n 's/^#define FW_VERSION *"\([^"]*\)".*/\1/p' src/config.h | tail -1)
[ -n "$VER" ] || { echo "FW_VERSION not found in src/config.h"; exit 1; }
REPO=$(sed -n 's/^#define GITHUB_REPO *"\([^"]*\)".*/\1/p' src/config.h)

if [ "${1:-}" = "--promote" ]; then
    gh release view "v$VER" -R "$REPO" >/dev/null 2>&1 || { echo "v$VER has not been released yet"; exit 1; }
    gh release edit "v$VER" -R "$REPO" --prerelease=false --latest
    echo "v$VER is now the latest release - clocks will install it tonight."
    exit 0
fi

if gh release view "v$VER" -R "$REPO" >/dev/null 2>&1; then
    echo "v$VER already exists - bump FW_VERSION in src/config.h first"
    exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
    echo "Commit your changes first, so the release matches the code on GitHub"
    exit 1
fi

if command -v pio >/dev/null 2>&1; then PIO=pio; else PIO="python -m platformio"; fi
$PIO run -e nodemcuv2

mkdir -p release
cp .pio/build/nodemcuv2/firmware.bin release/firmware.bin
printf '%s\n' "$VER" > release/version.txt

gh release create "v$VER" release/firmware.bin release/version.txt -R "$REPO" --prerelease \
    --title "v$VER" --notes "${1:-Firmware $VER}" --target "$(git rev-parse HEAD)"
echo
echo "Published v$VER as a PRE-release (clocks don't see it yet)."
echo "Test it: open your clock's web page -> Upload .bin -> release/firmware.bin"
echo "Then:    tools/release.sh --promote"
