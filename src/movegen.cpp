#include "movegen.hpp"

namespace chess {

namespace {

void add_promotions(MoveList& list, Square from, Square to) {
    list.add(Move::make(from, to, PROMOTION, QUEEN));
    list.add(Move::make(from, to, PROMOTION, ROOK));
    list.add(Move::make(from, to, PROMOTION, BISHOP));
    list.add(Move::make(from, to, PROMOTION, KNIGHT));
}

// ---------------------------------------------------------------------------
// Pawns
//
// Templating on color lets the compiler fold every direction and rank mask to
// a constant, so there is no per-move branching on which way pawns march.
// The mirror of NORTH_EAST (+9) for black is SOUTH_WEST (-9), which is why the
// same `to - UpRight` arithmetic recovers the origin square for both sides.
// ---------------------------------------------------------------------------

template<Color Us>
void generate_pawn_moves(const Position& pos, MoveList& list) {
    constexpr Color     Them    = ~Us;
    constexpr Direction Up      = (Us == WHITE ? NORTH      : SOUTH);
    constexpr Direction UpRight = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft  = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);
    constexpr Bitboard  Rank3BB = (Us == WHITE ? RANK_3_BB  : RANK_6_BB);
    constexpr Bitboard  Rank8BB = (Us == WHITE ? RANK_8_BB  : RANK_1_BB);

    const Bitboard pawns   = pos.pieces(Us, PAWN);
    const Bitboard empty   = ~pos.pieces();
    const Bitboard enemies = pos.pieces(Them);

    // Quiet pushes. A double push is legal only if the single push square was
    // also empty, which falls out of deriving push2 from push1.
    Bitboard push1 = shift<Up>(pawns) & empty;
    Bitboard push2 = shift<Up>(push1 & Rank3BB) & empty;

    while (push1) {
        const Square to   = pop_lsb(push1);
        const Square from = Square(int(to) - Up);
        if (square_bb(to) & Rank8BB) add_promotions(list, from, to);
        else                         list.add(Move(from, to));
    }
    while (push2) {
        const Square to = pop_lsb(push2);
        list.add(Move(Square(int(to) - 2 * Up), to));
    }

    // Captures
    Bitboard capRight = shift<UpRight>(pawns) & enemies;
    Bitboard capLeft  = shift<UpLeft>(pawns)  & enemies;

    while (capRight) {
        const Square to   = pop_lsb(capRight);
        const Square from = Square(int(to) - UpRight);
        if (square_bb(to) & Rank8BB) add_promotions(list, from, to);
        else                         list.add(Move(from, to));
    }
    while (capLeft) {
        const Square to   = pop_lsb(capLeft);
        const Square from = Square(int(to) - UpLeft);
        if (square_bb(to) & Rank8BB) add_promotions(list, from, to);
        else                         list.add(Move(from, to));
    }

    // En passant. PawnAttacks[Them][ep] is the set of squares from which one of
    // OUR pawns would be attacking ep -- the reverse-lookup trick again.
    if (const Square ep = pos.ep_square(); ep != SQ_NONE) {
        Bitboard attackers = pawns & PawnAttacks[Them][ep];
        while (attackers)
            list.add(Move::make(pop_lsb(attackers), ep, EN_PASSANT));
    }
}

// ---------------------------------------------------------------------------
// Knights, bishops, rooks, queens, king
// ---------------------------------------------------------------------------

template<PieceType Pt>
void generate_piece_moves(const Position& pos, MoveList& list, Bitboard target) {
    Bitboard from_bb = pos.pieces(pos.side_to_move(), Pt);
    while (from_bb) {
        const Square from = pop_lsb(from_bb);
        Bitboard attacks = attacks_bb(Pt, from, pos.pieces()) & target;
        while (attacks)
            list.add(Move(from, pop_lsb(attacks)));
    }
}

// ---------------------------------------------------------------------------
// Castling
//
// The only move generated here that needs full legality checking up front:
// a king may not start in check, pass through an attacked square, or land on
// one. The pass-through condition is invisible to the usual "is our king
// attacked after the move?" filter, so it must be tested here.
// ---------------------------------------------------------------------------

template<Color Us>
void generate_castling(const Position& pos, MoveList& list) {
    constexpr Square KingSq = (Us == WHITE ? SQ_E1     : SQ_E8);
    constexpr Square RookH  = (Us == WHITE ? SQ_H1     : SQ_H8);
    constexpr Square RookA  = (Us == WHITE ? SQ_A1     : SQ_A8);
    constexpr int    OO     = (Us == WHITE ? WHITE_OO  : BLACK_OO);
    constexpr int    OOO    = (Us == WHITE ? WHITE_OOO : BLACK_OOO);
    constexpr Color  Them   = ~Us;

    const Bitboard occ  = pos.pieces();
    const Piece    rook = make_piece(Us, ROOK);

    if ((pos.castling_rights() & OO) && pos.piece_on(RookH) == rook) {
        const Square f = Square(KingSq + 1), g = Square(KingSq + 2);
        if (!(occ & (square_bb(f) | square_bb(g)))
            && !pos.attacked_by(KingSq, Them, occ)
            && !pos.attacked_by(f, Them, occ)
            && !pos.attacked_by(g, Them, occ))
            list.add(Move::make(KingSq, g, CASTLING));
    }

    if ((pos.castling_rights() & OOO) && pos.piece_on(RookA) == rook) {
        // b1/b8 must be empty but need not be safe -- the king never visits it.
        const Square d = Square(KingSq - 1), c = Square(KingSq - 2), b = Square(KingSq - 3);
        if (!(occ & (square_bb(d) | square_bb(c) | square_bb(b)))
            && !pos.attacked_by(KingSq, Them, occ)
            && !pos.attacked_by(d, Them, occ)
            && !pos.attacked_by(c, Them, occ))
            list.add(Move::make(KingSq, c, CASTLING));
    }
}

template<Color Us>
void generate_all(const Position& pos, MoveList& list) {
    // Anywhere not occupied by one of our own pieces.
    const Bitboard target = ~pos.pieces(Us);

    generate_pawn_moves<Us>(pos, list);
    generate_piece_moves<KNIGHT>(pos, list, target);
    generate_piece_moves<BISHOP>(pos, list, target);
    generate_piece_moves<ROOK>(pos, list, target);
    generate_piece_moves<QUEEN>(pos, list, target);
    generate_piece_moves<KING>(pos, list, target);
    generate_castling<Us>(pos, list);
}

}  // namespace

void generate_pseudo_legal(const Position& pos, MoveList& list) {
    if (pos.side_to_move() == WHITE) generate_all<WHITE>(pos, list);
    else                             generate_all<BLACK>(pos, list);
}

void generate_legal(Position& pos, MoveList& list) {
    MoveList pseudo;
    generate_pseudo_legal(pos, pseudo);

    const Color us = pos.side_to_move();
    for (Move m : pseudo) {
        StateInfo st;
        pos.do_move(m, st);
        // do_move flipped the side; our king is the one that must be safe.
        if (!pos.attacked_by(pos.king_square(us), ~us, pos.pieces()))
            list.add(m);
        pos.undo_move(m);
    }
}

}  // namespace chess
