#pragma once

#include <cstdint>
#include "position.hpp"

namespace chess {

// Counts leaf nodes of the legal move tree at the given depth. This is the
// correctness oracle for move generation: the numbers are published for known
// positions, and any deviation means a bug -- not an approximation.
std::uint64_t perft(Position& pos, int depth);

// Same, but prints the node count under each root move. When a perft total is
// wrong, divide at the top, find the move whose subtotal disagrees with a
// reference engine, play it, and recurse. That bisection finds any movegen bug
// in a few minutes.
std::uint64_t perft_divide(Position& pos, int depth);

// Runs the six standard test positions. Returns true if every count matched.
bool run_perft_suite(int maxDepth);

}  // namespace chess
