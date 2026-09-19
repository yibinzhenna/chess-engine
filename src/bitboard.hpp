#pragma once

#include <bit>
#include "types.hpp"

namespace chess {

// ---------------------------------------------------------------------------
// File / rank masks
// ---------------------------------------------------------------------------

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;

constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_2_BB = RANK_1_BB << (8 * 1);
constexpr Bitboard RANK_3_BB = RANK_1_BB << (8 * 2);
constexpr Bitboard RANK_4_BB = RANK_1_BB << (8 * 3);
constexpr Bitboard RANK_5_BB = RANK_1_BB << (8 * 4);
constexpr Bitboard RANK_6_BB = RANK_1_BB << (8 * 5);
constexpr Bitboard RANK_7_BB = RANK_1_BB << (8 * 6);
constexpr Bitboard RANK_8_BB = RANK_1_BB << (8 * 7);

constexpr Bitboard square_bb(Square s) { return 1ULL << int(s); }

// ---------------------------------------------------------------------------
// Bit twiddling. <bit> is C++20 and compiles to a single instruction on any
// modern CPU -- no __builtin_/_BitScan intrinsic #ifdefs needed.
// ---------------------------------------------------------------------------

inline int    popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b)      { return Square(std::countr_zero(b)); }

inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;          // clears the lowest set bit
    return s;
}

// Shift a whole board one step in a direction, masking off the files that
// would otherwise wrap around the edge.
template<Direction D>
constexpr Bitboard shift(Bitboard b) {
    return D == NORTH      ?  b << 8
         : D == SOUTH      ?  b >> 8
         : D == EAST       ? (b & ~FILE_H_BB) << 1
         : D == WEST       ? (b & ~FILE_A_BB) >> 1
         : D == NORTH_EAST ? (b & ~FILE_H_BB) << 9
         : D == NORTH_WEST ? (b & ~FILE_A_BB) << 7
         : D == SOUTH_EAST ? (b & ~FILE_H_BB) >> 7
         : D == SOUTH_WEST ? (b & ~FILE_A_BB) >> 9
         : 0;
}

// ---------------------------------------------------------------------------
// Precomputed attack tables (filled by init()).
// ---------------------------------------------------------------------------

extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];  // KNIGHT and KING only

void init();

// Sliding attacks, computed by walking rays until they hit an occupied square.
// Correct and simple; this is the first thing to replace with magic or PEXT
// bitboards once perft passes and you start caring about speed.
Bitboard sliding_attacks(Square s, Bitboard occupied, bool diagonal);

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {
    switch (pt) {
        case BISHOP: return sliding_attacks(s, occupied, true);
        case ROOK:   return sliding_attacks(s, occupied, false);
        case QUEEN:  return sliding_attacks(s, occupied, true)
                          | sliding_attacks(s, occupied, false);
        default:     return PseudoAttacks[pt][s];   // KNIGHT, KING
    }
}

std::string pretty(Bitboard b);   // ASCII dump, for debugging

}  // namespace chess
