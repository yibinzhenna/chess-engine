#include "bitboard.hpp"

namespace chess {

Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

namespace {

// (file delta, rank delta) pairs
constexpr int KnightDeltas[8][2] = {
    {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
};
constexpr int KingDeltas[8][2] = {
    {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}
};

constexpr int BishopDirs[4][2] = { {1, 1}, {1, -1}, {-1, -1}, {-1, 1} };
constexpr int RookDirs[4][2]   = { {0, 1}, {1, 0}, {0, -1}, {-1, 0} };

}  // namespace

Bitboard sliding_attacks(Square s, Bitboard occupied, bool diagonal) {
    Bitboard attacks = 0;
    const int f0 = file_of(s), r0 = rank_of(s);
    const auto* dirs = diagonal ? BishopDirs : RookDirs;

    for (int i = 0; i < 4; ++i) {
        const int df = dirs[i][0], dr = dirs[i][1];
        int f = f0 + df, r = r0 + dr;
        // Walk outward until we leave the board or run into a piece. The
        // blocker's own square IS included -- it may be capturable.
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            const Square sq = make_square(File(f), Rank(r));
            attacks |= square_bb(sq);
            if (occupied & square_bb(sq)) break;
            f += df;
            r += dr;
        }
    }
    return attacks;
}

void init() {
    for (int i = SQ_A1; i <= SQ_H8; ++i) {
        const Square s = Square(i);
        const Bitboard b = square_bb(s);

        PawnAttacks[WHITE][s] = shift<NORTH_EAST>(b) | shift<NORTH_WEST>(b);
        PawnAttacks[BLACK][s] = shift<SOUTH_EAST>(b) | shift<SOUTH_WEST>(b);

        const int f0 = file_of(s), r0 = rank_of(s);
        for (int d = 0; d < 8; ++d) {
            const int kf = f0 + KnightDeltas[d][0], kr = r0 + KnightDeltas[d][1];
            if (kf >= 0 && kf < 8 && kr >= 0 && kr < 8)
                PseudoAttacks[KNIGHT][s] |= square_bb(make_square(File(kf), Rank(kr)));

            const int gf = f0 + KingDeltas[d][0], gr = r0 + KingDeltas[d][1];
            if (gf >= 0 && gf < 8 && gr >= 0 && gr < 8)
                PseudoAttacks[KING][s] |= square_bb(make_square(File(gf), Rank(gr)));
        }
    }
}

std::string pretty(Bitboard b) {
    std::string out = "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = RANK_8; r >= RANK_1; --r) {
        for (int f = FILE_A; f <= FILE_H; ++f)
            out += (b & square_bb(make_square(File(f), Rank(r)))) ? " | X" : " |  ";
        out += " | " + std::to_string(r + 1) + "\n +---+---+---+---+---+---+---+---+\n";
    }
    out += "   a   b   c   d   e   f   g   h\n";
    return out;
}

}  // namespace chess
