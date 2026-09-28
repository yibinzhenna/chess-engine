#!/usr/bin/env bash
# Compile the engine to WebAssembly for the browser.
#
#   source ~/emsdk/emsdk_env.sh
#   ./build-wasm.sh [output-dir]
#
# Output defaults to the personal-website chess page. The .js/.wasm pair are
# committed to that repo because GitHub Pages serves static files only -- there
# is no build step on deploy.

set -euo pipefail

OUT_DIR="${1:-../../personal website/personal-website/chess-engine}"
mkdir -p "$OUT_DIR"

# main.cpp is excluded (it owns main() and the UCI loop) and perft.cpp is
# excluded (a correctness tool, not needed in the browser).
SOURCES=(
  src/bitboard.cpp
  src/position.cpp
  src/movegen.cpp
  src/search.cpp
  src/wasm_api.cpp
)

EXPORTS='[
  "_engine_init","_engine_new_game","_engine_set_fen","_engine_fen",
  "_engine_board","_engine_legal_moves","_engine_side_to_move",
  "_engine_in_check","_engine_move_count","_engine_status",
  "_engine_make_move","_engine_undo","_engine_best_move",
  "_engine_play_best","_engine_evaluate","_malloc","_free"
]'

# em++ rather than emcc: emcc compiles .cpp as C++ but links against the C
# runtime, so every libc++ symbol comes back undefined.
em++ "${SOURCES[@]}" \
  -std=c++20 -O3 \
  -Isrc \
  --no-entry \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createChessEngine \
  -sEXPORTED_FUNCTIONS="$EXPORTS" \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT='web,worker' \
  -sFILESYSTEM=0 \
  -o "$OUT_DIR/chess-engine.js"

echo
echo "Built into $OUT_DIR:"
ls -lh "$OUT_DIR"/chess-engine.js "$OUT_DIR"/chess-engine.wasm | awk '{print "  " $9 "  " $5}'
