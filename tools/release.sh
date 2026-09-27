#!/usr/bin/env bash
# Build the firmware and publish it as a GitHub release.
# Every clock with "Automatic updates" on installs it the following night
# (or right away via "Check now" -> "Install update" on its web page).
#
# Usage:  tools/release.sh "What changed in this version"
# Before: bump FW_VERSION in src/config.h and commit + push.
set -euo pipefail
cd "$(dirname "$0")/.."

VER=$(sed -n 's/^#define FW_VERSION *"\([^"]*\)".*/\1/p' src/config.h)
[ -n "$VER" ] || { echo "FW_VERSION not found in src/config.h"; exit 1; }
REPO=$(sed -n 's/^#define GITHUB_REPO *"\([^"]*\)".*/\1/p' src/config.h)

if gh release view "v$VER" -R "$REPO" >/dev/null 2>&1; then
    echo "v$VER is already released - bump FW_VERSION in src/config.h first"
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

gh release create "v$VER" release/firmware.bin release/version.txt -R "$REPO" \
    --title "v$VER" --notes "${1:-Firmware $VER}" --target "$(git rev-parse HEAD)"
echo "Released v$VER - clocks will pick it up automatically."
