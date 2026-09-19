#pragma once

#include <string>
#include "bitboard.hpp"

namespace chess {

constexpr const char* START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Irreversible state. do_move() cannot reconstruct these on the way back up,
// so each ply pushes one of these onto a stack (here: a caller-owned local).
struct StateInfo {
    int        castlingRights = NO_CASTLING;
    Square     epSquare       = SQ_NONE;
    int        halfmoveClock  = 0;
    Piece      capturedPiece  = NO_PIECE;
    StateInfo* previous       = nullptr;
};

class Position {
public:
    Position() { set(START_FEN); }

    void set(const std::string& fen);
    std::string fen() const;
    std::string to_string() const;

    // --- queries -----------------------------------------------------------
    Bitboard pieces()                     const { return byType_[NO_PIECE_TYPE]; }
    Bitboard pieces(PieceType pt)         const { return byType_[pt]; }
    Bitboard pieces(Color c)              const { return byColor_[c]; }
    Bitboard pieces(Color c, PieceType pt) const { return byType_[pt] & byColor_[c]; }

    Piece  piece_on(Square s)  const { return board_[s]; }
    bool   empty(Square s)     const { return board_[s] == NO_PIECE; }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }

    Color  side_to_move()     const { return sideToMove_; }
    Square ep_square()        const { return st_->epSquare; }
    int    castling_rights()  const { return st_->castlingRights; }
    int    halfmove_clock()   const { return st_->halfmoveClock; }
    int    fullmove_number()  const { return fullmoveNumber_; }

    // Is square `s` attacked by any piece of color `by`, given `occupied`?
    bool attacked_by(Square s, Color by, Bitboard occupied) const;
    bool in_check() const {
        return attacked_by(king_square(sideToMove_), ~sideToMove_, pieces());
    }

    // --- mutation ----------------------------------------------------------
    // `newSt` must outlive the move (keep it as a local in the caller's frame).
    void do_move(Move m, StateInfo& newSt);
    void undo_move(Move m);

private:
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);

    // byType_[NO_PIECE_TYPE] doubles as the "all occupied squares" board.
    Bitboard  byType_[PIECE_TYPE_NB] {};
    Bitboard  byColor_[COLOR_NB]     {};
    Piece     board_[SQUARE_NB]      {};
    Color     sideToMove_    = WHITE;
    int       fullmoveNumber_ = 1;
    StateInfo rootState_     {};
    StateInfo* st_           = &rootState_;
};

}  // namespace chess
