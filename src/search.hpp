#pragma once

#include "position.hpp"

namespace chess {

constexpr int VALUE_MATE = 30000;
constexpr int VALUE_INF  = 32000;

struct SearchLimits {
    int       depth      = 64;   // hard cap on iterative deepening
    long long movetimeMs = 0;    // fixed time for this move, 0 = derive from clocks
    long long time[COLOR_NB] {0, 0};
    long long inc[COLOR_NB]  {0, 0};
};

// Static evaluation, in centipawns, from the side-to-move's point of view.
int evaluate(const Position& pos);

// Iterative-deepening alpha-beta search. Prints UCI `info` lines as it goes
// and returns the best move found before the clock ran out.
Move search_best_move(Position& pos, const SearchLimits& limits);

}  // namespace chess
