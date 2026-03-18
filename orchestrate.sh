#!/usr/bin/env bash
# orchestrate.sh — end-to-end showcase of DLR + RCP hot-reload
#
# Builds everything, starts the program, then injects live source changes to
# both DLR and RCP while the process keeps running — no restart required.
#
# Usage:  ./orchestrate.sh

set -euo pipefail

BUILD_DIR="build"
LOG="$BUILD_DIR/demo.log"

# ── terminal colours ──────────────────────────────────────────────────────────
BOLD='\033[1m'; CYAN='\033[0;36m'; GREEN='\033[0;32m'
YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'

banner() { echo -e "\n${BOLD}${CYAN}══ $1 ══${NC}"; }
step()   { echo -e "\n${BOLD}${GREEN}▶ $1${NC}"; }
info()   { echo -e "  ${BLUE}$1${NC}"; }

# ── save originals so the demo is repeatable ──────────────────────────────────
DLR_ORIG=$(cat dlr/plugin.cpp)
RCP_ORIG=$(cat rcp/patch_plugin.cpp)

restore() {
    printf '%s\n' "$DLR_ORIG" > dlr/plugin.cpp
    printf '%s\n' "$RCP_ORIG" > rcp/patch_plugin.cpp
}
trap restore EXIT

show_log() {
    local lines=${1:-12}
    echo -e "\n${YELLOW}┌─ hot_reload output (last $lines lines) ─────────────────────────┐${NC}"
    tail -n "$lines" "$LOG" | sed 's/^/│ /'
    echo -e "${YELLOW}└──────────────────────────────────────────────────────────────────┘${NC}"
}

# ── Step 1: build ─────────────────────────────────────────────────────────────
banner "Step 1 / 5 — Full build"
./build.sh
cmake --build "$BUILD_DIR" --target patch_raw 2>&1 | tail -2

# ── Step 2: launch ────────────────────────────────────────────────────────────
banner "Step 2 / 5 — Launch hot_reload"
step "Starting process (output → $LOG)"
rm -f "$LOG"
stdbuf -oL ./build/hot_reload > "$LOG" 2>&1 &
APP_PID=$!
info "PID $APP_PID"

# The benchmark phase takes ~5 s; wait for it to finish and the loop to start.
step "Waiting for startup phases (DLR, RCP, benchmark, new-functions)..."
for i in $(seq 1 14); do
    sleep 1
    printf "  %2ds / 14s\r" "$i"
done
echo ""

show_log 20

# ── Step 3: DLR change ────────────────────────────────────────────────────────
banner "Step 3 / 5 — DLR hot-reload: change plugin_compute"
step "Editing dlr/plugin.cpp:  x*2  →  x*7"
sed -i 's/return x \* 2;/return x * 7;/' dlr/plugin.cpp

step "Rebuilding libplugin.so (only this target)..."
./build.sh plugin

info "Waiting for the running process to detect the .so change..."
sleep 6
show_log 8

# ── Step 4: RCP change ────────────────────────────────────────────────────────
banner "Step 4 / 5 — RCP hot-reload: change patched_compute"
step "Editing rcp/patch_plugin.cpp:  x*11  →  x*x + 1  (linear → quadratic)"
sed -i 's/return x \* 11;/return x * x + 1;/' rcp/patch_plugin.cpp

step "Rebuilding libpatch_plugin.so (hybrid RCP)..."
./build.sh patch

step "Rebuilding patch_snippet.bin (raw byte injection)..."
./build.sh raw

info "Waiting for the running process to re-patch the trampoline..."
sleep 6
show_log 8

# ── Step 5: stop ──────────────────────────────────────────────────────────────
banner "Step 5 / 5 — Stop"
kill "$APP_PID" 2>/dev/null || true
wait "$APP_PID" 2>/dev/null || true

echo -e "\n${BOLD}Demo complete.${NC}"
echo -e "  Full log:  ${BLUE}$LOG${NC}"
echo ""
echo "  Changes applied while PID $APP_PID ran without interruption:"
echo "    DLR  — plugin_compute:   x*2  →  x*7   (libplugin.so swapped)"
echo "    RCP  — patched_compute:  x*11 →  x*x+1 (trampoline rewritten in-place)"
echo ""
echo -e "${YELLOW}Source files restored to original state (run is repeatable).${NC}"
