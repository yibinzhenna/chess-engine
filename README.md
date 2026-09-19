# Chess engine — MVP scaffold

A minimal but complete UCI chess engine in C++20: bitboard move generation
verified by perft, alpha-beta search with quiescence, and enough of the UCI
protocol to load into a real chess GUI.

~1,100 lines. Every part is meant to be replaced as you learn.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

With MSYS2/GCC, add `-G Ninja` for a faster build.

## Run

```
build/chess test          # perft suite — run this FIRST
build/chess test 5        # deeper, slower
build/chess divide 3      # per-root-move node counts, for debugging movegen
build/chess bench         # search sanity check
build/chess               # UCI loop (what a GUI talks to)
```

In the UCI loop, `d` prints the board and `eval` prints the static evaluation.

## Is it correct?

Run `chess test`. Perft counts nodes in the legal move tree; the expected
numbers are published and exact. If they match at depth 4–5 across all six
positions, move generation is almost certainly correct. If one is off:

1. `chess divide 3 "<fen>"` on the failing position.
2. Compare each root move's subtotal against a reference engine
   (Stockfish supports `go perft 3`).
3. Play the move whose subtotal disagrees, and repeat one ply shallower.

A few rounds of that isolates any movegen bug to a single position.

## Layout

| File | Responsibility |
|---|---|
| `src/types.hpp` | Colors, pieces, squares, the packed 16-bit `Move` |
| `src/bitboard.*` | 64-bit board ops, attack tables, sliding attacks |
| `src/position.*` | Board state, FEN, attack detection, `do_move`/`undo_move` |
| `src/movegen.*` | Pseudo-legal generation, then legality filtering |
| `src/perft.*` | Correctness oracle + the standard test suite |
| `src/search.*` | Evaluation, alpha-beta, quiescence, iterative deepening |
| `src/main.cpp` | UCI protocol and the CLI |

## Known limitations (all deliberate)

- **No transposition table.** Needs Zobrist hashing first.
- **No repetition detection.** The engine can walk into a draw by repetition
  without noticing. Needs Zobrist keys plus a position history.
- **Sliding attacks are computed by ray-walking**, not magic or PEXT bitboards.
  Correct, but the single biggest speed win available.
- **Legality by make/unmake.** Generating only legal moves directly (with pin
  and check masks) is roughly 2–3x faster.
- **Search is a plain alpha-beta.** No null-move pruning, late move reductions,
  killer moves, or history heuristic.
- **Evaluation is material + piece-square tables only.** No pawn structure,
  king safety, or mobility.
- **Single-threaded**, and `stop` during a search is not honored (the search
  runs on the main thread).

## Roadmap

1. ✅ Bitboards, move generation, perft
2. ✅ UCI, alpha-beta, quiescence, iterative deepening
3. Zobrist hashing → transposition table → repetition detection
4. Better move ordering: killers, history, TT move first
5. Magic or PEXT sliding attacks
6. Null-move pruning, late move reductions
7. Texel-tune the evaluation weights against real game results
8. SPRT testing with `cutechess-cli` so every change is measured, not guessed
