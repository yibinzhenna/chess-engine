# chess-engine

A chess engine written in C++20.

## What a chess engine is

A chess engine is a program that, given a chess position, decides what move to
play. It has no graphical board of its own — instead it speaks a text protocol
(UCI) over standard input and output, so any chess GUI can drive it, and any
two engines can be made to play each other automatically.

Every engine, from a weekend project to a world champion, is built from the
same four parts.

### Board representation

How the position is stored in memory. The standard approach is **bitboards**: a
64-bit integer per piece type, where bit *i* being set means "there is a piece
of this type on square *i*." Because a chess board has exactly 64 squares and a
machine word has exactly 64 bits, questions like "which squares can this rook
reach?" or "how many pawns does white have?" reduce to single CPU
instructions rather than loops.

### Move generation

Producing the list of legal moves in a position. This is the part most engines
get wrong first, because chess has more edge cases than it appears: en passant,
castling rights and the squares the king passes through, promotion, pinned
pieces, and check evasion all interact.

Correctness here is verifiable rather than a matter of judgment. **Perft** — a
count of the leaf nodes in the legal move tree at a given depth — has exact,
published values for standard positions. If the engine's count differs by even
one node, there is a bug.

### Search

Looking ahead. The engine explores the tree of possible continuations, assuming
both sides play their best move, and picks the branch that leads to the best
outcome. The core is **minimax**, made tractable by **alpha-beta pruning**,
which discards branches that cannot affect the result — often reducing the work
from *N* nodes to roughly the square root of *N*.

Almost all of an engine's strength comes from search: how deep it can look, and
how aggressively it can prune without discarding something important.

### Evaluation

Scoring a position the search cannot look past. A simple evaluation counts
material (a queen is worth about nine pawns) and adds positional terms: knights
belong near the center, rooks belong on open files, the king should be castled
in the middlegame and active in the endgame. Modern engines replace most of
this with a small neural network trained on millions of positions.

Search and evaluation trade off against each other. A faster, shallower
evaluation lets the engine search deeper in the same time, and depth usually
wins.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Requires a C++20 compiler (GCC 11+, Clang 13+, or MSVC 2022) and CMake 3.20+.

## Usage

```
build/chess test          # run the perft correctness suite
build/chess divide 3      # per-root-move node counts, for debugging movegen
build/chess bench         # search sanity check
build/chess               # UCI loop — what a chess GUI connects to
```

To play against it, point a UCI-compatible GUI such as Cute Chess, Arena, or
BanksiaGUI at the built executable.

## Layout

| Path | Responsibility |
|---|---|
| `src/types.hpp` | Colors, pieces, squares, the packed 16-bit move encoding |
| `src/bitboard.*` | 64-bit board operations and attack tables |
| `src/position.*` | Board state, FEN parsing, attack detection, make/unmake |
| `src/movegen.*` | Legal move generation |
| `src/perft.*` | Correctness oracle and the standard test suite |
| `src/search.*` | Evaluation, alpha-beta, quiescence, iterative deepening |
| `src/main.cpp` | UCI protocol and command-line interface |

---

# Update log

Newest first. Each entry records what changed and what it enabled.

## 2026-09-19 — Initial scaffold

First working version: a complete engine skeleton, from board representation
through to a UCI interface a GUI can talk to.

**Board and move generation**
- Bitboard representation with a redundant square-indexed array for fast
  piece lookup
- Precomputed pawn, knight and king attack tables
- Sliding attacks (bishop, rook, queen) by ray-walking — correct, not yet fast
- 16-bit packed move encoding covering normal moves, promotions, en passant
  and castling
- Full FEN parsing and generation
- `do_move` / `undo_move` with a caller-owned state stack, so the search
  mutates one board in place rather than copying
- Pseudo-legal generation followed by a legality filter; castling legality
  (including squares the king passes through) checked at generation time

**Correctness**
- `perft` and `perft_divide`
- The six standard Chess Programming Wiki test positions with expected node
  counts, wired to `ctest`

**Search and evaluation**
- Negamax with alpha-beta pruning
- Quiescence search to resolve captures past the search horizon
- Iterative deepening with time management; partial iterations discarded
- MVV-LVA move ordering
- Material plus piece-square table evaluation
- Mate scores adjusted by ply, so faster mates are preferred

**Interface**
- UCI: `uci`, `isready`, `ucinewgame`, `position`, `go`, `quit`, plus
  `go perft`
- Non-standard `d` and `eval` commands for debugging
- CLI subcommands: `test`, `perft`, `divide`, `bench`

**Build**
- CMake targeting C++20, defaulting to Release
- Optional native-CPU tuning (`-march=native` / `/arch:AVX2`) so
  `std::popcount` and `std::countr_zero` compile to single instructions
- `ctest` runs the perft suite

**Not yet included** — each a natural next step:
- Zobrist hashing, and therefore no transposition table and no repetition
  detection
- Magic or PEXT bitboards for sliding attacks
- Killer moves, history heuristic, null-move pruning, late move reductions
- Positional evaluation beyond piece-square tables
- Multithreading; `stop` is not honored mid-search
