#pragma once

#include "position.hpp"

namespace chess {

// A fixed-capacity move buffer. 218 is the largest number of legal moves any
// legal chess position has been shown to have; 256 gives headroom for the
// pseudo-legal list. Stack-allocated, so no allocation in the search hot path.
struct MoveList {
    Move moves[256];
    int  count = 0;

    void add(Move m) { moves[count++] = m; }
    int  size() const { return count; }
    bool empty() const { return count == 0; }

    Move  operator[](int i) const { return moves[i]; }
    const Move* begin() const { return moves; }
    const Move* end()   const { return moves + count; }
};

// Moves that follow piece movement rules but may leave our own king in check.
void generate_pseudo_legal(const Position& pos, MoveList& list);

// Fully legal moves. Filters the pseudo-legal list by playing each move and
// checking whether our king is attacked afterwards.
void generate_legal(Position& pos, MoveList& list);

}  // namespace chess
