#pragma once

#include <cstdint>
#include <string>

#include "position.hpp"

namespace chess {

constexpr int VALUE_MATE = 30000;
constexpr int VALUE_INF  = 32000;
constexpr int MAX_PLY    = 128;

// ---------------------------------------------------------------------------
// Difficulty
//
// Capping search depth alone makes a bot that is weak in an inhuman way: it
// still finds every short tactic perfectly, then walks into a longer one. A
// convincing weak opponent instead makes PLAUSIBLE mistakes, which comes from
// layering several independent handicaps.
// ---------------------------------------------------------------------------

enum class Difficulty { Beginner, Intermediate, Advanced, MagnusCarlsen, NB };

struct Skill {
    const char* name;
    int  maxDepth;        // hard cap on iterative deepening
    long long maxMoveMs;  // per-move time cap; 0 means "use the clock"
    int  evalNoise;       // +/- centipawns of uniform noise added to root scores
    int  blunderPercent;  // chance of deliberately not playing the best move
    int  topN;            // how many root moves a blunder may choose among
    bool useQuiescence;   // off => the bot hangs pieces to recaptures
};

Skill       skill_for(Difficulty d);
const char* difficulty_name(Difficulty d);

// Accepts "beginner", "advanced", "magnus", "magnus carlsen", "carlsen", ...
// Case-insensitive. Returns false if the name is not recognised.
bool difficulty_from_string(const std::string& s, Difficulty& out);

struct SearchLimits {
    int       depth      = 64;   // hard cap on iterative deepening
    long long movetimeMs = 0;    // fixed time for this move, 0 = derive from clocks
    long long time[COLOR_NB] {0, 0};
    long long inc[COLOR_NB]  {0, 0};

    Difficulty   difficulty = Difficulty::MagnusCarlsen;
    std::uint64_t seed      = 0;  // 0 = seed randomly; non-zero = reproducible
    bool         silent     = false;  // suppress `info` lines (used by `match`)
};

// Static evaluation, in centipawns, from the side-to-move's point of view.
int evaluate(const Position& pos);

// Iterative-deepening alpha-beta search. Prints UCI `info` lines as it goes
// and returns the move the configured difficulty decides to play -- which is
// not always the best one it found.
Move search_best_move(Position& pos, const SearchLimits& limits);

}  // namespace chess
