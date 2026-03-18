#!/usr/bin/env bash
# build.sh — helper for the hot-reload PoC
#
# Usage:
#   ./build.sh           — full configure + build
#   ./build.sh plugin    — rebuild libplugin.so only      (DLR hot-reload demo)
#   ./build.sh patch     — rebuild libpatch_plugin.so only (RCP hot-reload demo)
#   ./build.sh raw       — compile patch_plugin.cpp to raw .bin  (RawInject demo)
#   ./build.sh run       — full build then run

set -euo pipefail

BUILD_DIR="build"

full_build() {
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build "$BUILD_DIR" -- -j"$(nproc)"
    echo ""
    echo "Build complete."
    echo "  Run:          cd $BUILD_DIR && ./hot_reload"
    echo "  Reload DLR:   cmake --build $BUILD_DIR --target plugin"
}

case "${1:-}" in
    plugin)
        cmake --build "$BUILD_DIR" --target plugin
        echo "DLR plugin rebuilt: $BUILD_DIR/libplugin.so"
        ;;
    patch)
        cmake --build "$BUILD_DIR" --target patch_plugin
        echo "RCP patch plugin rebuilt: $BUILD_DIR/libpatch_plugin.so"
        ;;
    raw)
        cmake --build "$BUILD_DIR" --target patch_raw
        echo "Raw machine code: $BUILD_DIR/patch_snippet.bin"
        ;;
    run)
        full_build
        cd "$BUILD_DIR" && ./hot_reload
        ;;
    *)
        full_build
        ;;
esac
