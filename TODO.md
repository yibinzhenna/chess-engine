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
| Quiescence search disabled | Hangs pieces to recaptures | A genuine beginner |

That last one is worth calling out: turning off quiescence reproduces the single
most characteristic beginner mistake — trading into a losing recapture — without
any artificial randomness at all.

**Blunders must stay plausible.** Picking a random legal move reads instantly as
a bot. Picking the third-best move, or missing a knight fork, reads as a person.

### Tasks

- [ ] Add a `Skill` struct: max depth, node cap, eval noise sigma, blunder
      probability, top-N selection width, quiescence on/off
- [ ] Thread it through `SearchLimits` into `search_best_move`
- [ ] **Score every root move.** Alpha-beta only returns an exact score for the
      best move; the rest are bounds, which is useless for "pick the third
      best". Needs either a full-window search at the root or a MultiPV mode.
      *This is the real work item — everything else is tuning.*
- [ ] Weighted random selection among root moves within a centipawn window
- [ ] Deterministic seeding, so a game can be replayed for debugging
- [ ] Named presets: Beginner, Intermediate, Experienced, Pro
- [ ] Implement the standard UCI options `UCI_LimitStrength` (check) and
      `UCI_Elo` (spin) — chess GUIs render these as a built-in strength slider,
      so the levels work in Cute Chess and Arena with no custom UI
- [ ] Add a `Skill Level` spin option (0–20) as the finer-grained control
- [ ] Handle `setoption` in the UCI loop (not yet implemented at all)

### Target ladder

| Preset | Target Elo | Rough recipe |
|---|---|---|
| Beginner | 600–800 | Depth 2, no quiescence, heavy eval noise, frequent blunders |
| Intermediate | 1200–1400 | Depth 4, quiescence on, moderate noise, occasional blunder |
| Experienced | 1700–1900 | Depth 6–8, light noise, rare blunder |
| Pro | Full strength | No handicap, full time control |

**Note the dependency:** "Pro" is defined by whatever the engine can actually
do, which today is roughly 1600–1800. Every item in section 2 raises that
ceiling and shifts the whole ladder up. The named presets should be re-measured
after each strength improvement rather than assumed to hold.

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
