# TODO

Working list, roughly in priority order. Check items off as they land and add a
matching entry to the update log in `README.md`.

---

## 1. Difficulty levels (bots you can play against)

The headline feature: selectable opponents from beginner up to full strength.

### The design problem

The obvious approach — cap the search depth — produces bots that are weak in a
way that feels wrong. A depth-2 engine still finds every two-move tactic
perfectly, then hangs its queen to a three-move one. It is weak *and* alien.

A convincing weak opponent makes **plausible** mistakes: it misses things, it
misjudges positions, it occasionally picks the second-best move. That comes
from layering several handicaps rather than turning one dial:

| Handicap | Effect on play | Feels like |
|---|---|---|
| Reduced depth / nodes | Limits tactical vision | Missing deep combinations |
| Evaluation noise (± centipawns) | Fuzzy positional judgment | Inconsistent plans |
| Weighted random pick among top moves | Doesn't always find the best | Human inaccuracy |
| Occasional deliberate 2nd/3rd-best move | Real mistakes at intervals | Losing the thread |
| Quiescence search disabled | Misjudges every exchange | A genuine beginner |

That last one is worth calling out. With quiescence off, leaf positions are
scored mid-exchange with every pending capture ignored, so the bot both walks
into losing trades and shies away from good ones — it misjudges material the
way a real beginner does, with no artificial randomness at all.

**Blunders must stay plausible.** Picking a random legal move reads instantly as
a bot. Picking the third-best move, or missing a knight fork, reads as a person.

### Tasks

- [x] Add a `Skill` struct: max depth, per-move time cap, eval noise, blunder
      probability, top-N selection width, quiescence on/off
- [x] Thread it through `SearchLimits` into `search_best_move`
- [x] **Score every root move.** Alpha-beta only returns an exact score for the
      best move; the rest are bounds, which is useless for "pick the third
      best". Implemented as a full-window root search, enabled only when a
      handicap is active — it costs roughly 5x the nodes at equal depth, so
      full strength keeps the narrowed window.
- [x] Random selection among the top N root moves, after eval noise reorders
      them. *Possible refinement: weight the choice by score gap (softmax)
      instead of picking uniformly from the top N.*
- [x] Deterministic seeding, so a game can be replayed for debugging
- [x] Named presets: Beginner, Intermediate, Advanced, Magnus Carlsen
- [x] Handle `setoption` in the UCI loop
- [x] `chess match <a> <b> [games] [ms]` for engine-vs-engine ladder checks
- [ ] Implement the standard UCI options `UCI_LimitStrength` (check) and
      `UCI_Elo` (spin) — chess GUIs render these as a built-in strength slider,
      so the levels work in Cute Chess and Arena with no custom UI
- [ ] Add a `Skill Level` spin option (0–20) as the finer-grained control

### Target ladder

| Preset | Target Elo | Recipe as implemented |
|---|---|---|
| Beginner | 600–800 | Depth 2, 200ms, no quiescence, ±150cp noise, 35% blunder, top 6 |
| Intermediate | 1200–1400 | Depth 4, 500ms, ±60cp noise, 15% blunder, top 4 |
| Advanced | 1700–1900 | Depth 7, 1500ms, ±20cp noise, 5% blunder, top 3 |
| Magnus Carlsen | Full strength | No handicap, full clock |

Measured head-to-head at 30ms/move (ladder ordering only — these are **not**
Elo measurements against a rated opponent):

| Matchup | Result |
|---|---|
| Intermediate vs Beginner | 20–0 (100%) |
| Advanced vs Intermediate | 16–4 (80%) |
| Magnus Carlsen vs Advanced | 17–2, 1 draw (87.5%) |
| Magnus Carlsen vs Beginner | 10–0 (100%) |

**Note the dependency:** "Magnus Carlsen" means full strength, which is
whatever the engine can actually do — today roughly 1600–1800, nowhere near the
name. Every item in section 2 raises that ceiling and shifts the whole ladder
up, so the presets should be re-measured after each strength improvement rather
than assumed to hold.

### Calibration

Elo targets are claims, and claims need measurement:

- [ ] Set up `cutechess-cli` for automated engine-vs-engine matches
- [ ] Measure each preset against a reference of known strength (Stockfish with
      its own `UCI_Elo` set, or a rated engine from the CCRL lists)
- [ ] 500+ games per preset — anything less has error bars wider than the gaps
      between levels
- [ ] Record measured Elo in the README, not target Elo

---

## 2. Strength

Each of these raises the ceiling that "Pro" is measured against.

- [ ] **Zobrist hashing** — incremental position keys. Prerequisite for the
      next two items.
- [ ] **Transposition table** — cache searched positions. Typically the single
      largest Elo gain available at this stage.
- [ ] **Repetition detection** — the engine can currently walk into a draw by
      repetition without noticing, or miss one when losing.
- [ ] Better move ordering: TT move first, killer moves, history heuristic
- [ ] Null-move pruning
- [ ] Late move reductions
- [ ] Aspiration windows around the previous iteration's score
- [ ] Principal variation collection, so `info pv` shows the full line rather
      than just the best move
- [ ] Evaluation beyond piece-square tables: pawn structure, king safety,
      mobility, passed pawns
- [ ] Separate midgame/endgame evaluation with a phase taper — the current king
      table wants the king hiding in a corner even in a pawn endgame, which is
      actively wrong

---

## 3. Correctness and robustness

- [ ] Honor `stop` mid-search — requires moving search off the main thread
- [ ] `go infinite` and `ponder` support
- [ ] Reject malformed FEN rather than constructing a broken position
- [ ] Handle the fifty-move rule and insufficient material as game-end
      conditions, not just search cutoffs
- [ ] Unit tests beyond perft: FEN round-tripping, do/undo restoring exact
      state, evaluation symmetry under color flip

---

## 4. Speed

- [ ] **Magic or PEXT bitboards** for sliding attacks, replacing the current
      ray-walking. The i7-12650H is Alder Lake, where PEXT is fast — worth
      roughly 2–3x on move generation.
- [ ] Generate only legal moves directly (pin and check masks) instead of
      pseudo-legal plus filter
- [ ] Staged move generation — try the TT move before generating anything else
- [ ] Lazy SMP multithreading (16 threads available on this machine)

---

## 5. Testing infrastructure

- [ ] `cutechess-cli` harness with a standard opening book
- [ ] SPRT testing, so every change is accepted or rejected on measured Elo
      rather than intuition
- [ ] A bench command with a fixed node count, for comparing speed between
      commits
- [ ] CI that runs the perft suite on every push

---

## 6. Nice to have

- [ ] Opening book (Polyglot format)
- [ ] Syzygy endgame tablebase probing
- [ ] NNUE evaluation
- [ ] A simple GUI or web front end, so the difficulty levels are playable
      without installing a separate chess program
