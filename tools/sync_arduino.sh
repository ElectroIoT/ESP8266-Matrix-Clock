#!/usr/bin/env bash
# Regenerate the Arduino IDE sketch (arduino/MatrixClock) from src/.
# Run after changing anything in src/, then commit both.
set -euo pipefail
cd "$(dirname "$0")/.."

D=arduino/MatrixClock
mkdir -p "$D"
find "$D" -maxdepth 1 -type f ! -name private_config.h -delete

for f in src/*.h src/*.cpp; do
    b=$(basename "$f")
    case "$b" in main.cpp|private_config.h) continue ;; esac
    cp "$f" "$D/$b"
done
cat tools/arduino_header.txt src/main.cpp > "$D/MatrixClock.ino"
echo "Arduino sketch updated in $D"
