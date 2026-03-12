#!/usr/bin/env bash
# BLE GUI v0.1 - Linux Cross-Compile Script
# Requires: sudo apt install mingw-w64

set -e

COMPILER="x86_64-w64-mingw32-g++"
OUT="ble_gui.exe"
RC_FILE="app.rc"
RC_OBJ="app.res.o"

echo "[BLE v0.1] Cross-compiling for Windows (64-bit)..."

# Compile icon resource if windres and app.rc exist
if command -v x86_64-w64-mingw32-windres &>/dev/null && [ -f "$RC_FILE" ]; then
    echo "[RC] Compiling resource..."
    x86_64-w64-mingw32-windres "$RC_FILE" -O coff -o "$RC_OBJ"
    EXTRA_OBJ="$RC_OBJ"
else
    echo "[RC] Skipping resource (no .rc file or windres missing)"
    EXTRA_OBJ=""
fi

$COMPILER -std=c++17 \
    -mwindows \
    -municode \
    -O2 \
    -static \
    -static-libgcc \
    -static-libstdc++ \
    -Wall \
    -Wno-unknown-pragmas \
    -o "$OUT" \
    main.cpp \
    gui.cpp \
    ble.cpp \
    api.cpp \
    $EXTRA_OBJ \
    -lbthprops \
    -lsetupapi \
    -lws2_32 \
    -lole32 \
    -lcomctl32 \
    -ldwmapi \
    -lcomdlg32 \
    -lshlwapi \
    -lgdiplus \
    -lruntimeobject

echo "[OK] Done: $OUT"
