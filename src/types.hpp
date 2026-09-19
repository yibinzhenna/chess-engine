#pragma once

#include <cstdint>
#include <string>

namespace chess {

using Bitboard = std::uint64_t;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

enum Color : int { WHITE, BLACK, COLOR_NB = 2 };

constexpr Color operator~(Color c) { return Color(c ^ BLACK); }

// ---------------------------------------------------------------------------
// Pieces
//
// A Piece packs color and type into one value: (color << 3) | type. That keeps
// board_[] a single byte-ish array and makes type_of/color_of branch-free.
// ---------------------------------------------------------------------------

enum PieceType : int {
    NO_PIECE_TYPE = 0,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    PIECE_TYPE_NB = 7
};

enum Piece : int {
    NO_PIECE = 0,
    W_PAWN = PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = PAWN + 8, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

constexpr Piece     make_piece(Color c, PieceType pt) { return Piece((c << 3) + pt); }
constexpr PieceType type_of(Piece p)                  { return PieceType(p & 7); }
constexpr Color     color_of(Piece p)                 { return Color(p >> 3); }  // invalid for NO_PIECE

// ---------------------------------------------------------------------------
// Squares. A1 = 0, B1 = 1, ... H8 = 63. Rank is the high 3 bits, file the low 3.
// ---------------------------------------------------------------------------

enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE,
    SQUARE_NB = 64
};

enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8 };

constexpr File   file_of(Square s)            { return File(s & 7); }
constexpr Rank   rank_of(Square s)            { return Rank(s >> 3); }
constexpr Square make_square(File f, Rank r)  { return Square((r << 3) + f); }
constexpr bool   is_ok(Square s)              { return s >= SQ_A1 && s <= SQ_H8; }

// ---------------------------------------------------------------------------
// Directions, as offsets added to a square index.
// ---------------------------------------------------------------------------

enum Direction : int {
    NORTH =  8,
    EAST  =  1,
    SOUTH = -8,
    WEST  = -1,

    NORTH_EAST = NORTH + EAST,   //  9
    NORTH_WEST = NORTH + WEST,   //  7
    SOUTH_EAST = SOUTH + EAST,   // -7
    SOUTH_WEST = SOUTH + WEST    // -9
};

// ---------------------------------------------------------------------------
// Castling rights, held as a 4-bit mask.
// ---------------------------------------------------------------------------

enum CastlingRights : int {
    NO_CASTLING = 0,
    WHITE_OO    = 1,
    WHITE_OOO   = 2,
    BLACK_OO    = 4,
    BLACK_OOO   = 8,
    ANY_CASTLING = WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO
};

// ---------------------------------------------------------------------------
// Moves, packed into 16 bits:
//
//   bits  0-5   origin square
//   bits  6-11  destination square
//   bits 12-13  promotion piece, encoded as (piece - KNIGHT)
//   bits 14-15  move type
//
// Castling is encoded as the KING's two-square move (e1->g1, e1->c1). That is
// simpler than Stockfish's king-takes-rook encoding, at the cost of not being
// extensible to Chess960 later.
// ---------------------------------------------------------------------------

enum MoveType : int {
    NORMAL     = 0,
    PROMOTION  = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING   = 3 << 14
};

class Move {
public:
    Move() = default;
    constexpr explicit Move(std::uint16_t d) : data_(d) {}
    constexpr Move(Square from, Square to)
        : data_(std::uint16_t(int(from) | (int(to) << 6))) {}

    static constexpr Move make(Square from, Square to, MoveType mt,
                               PieceType promo = KNIGHT) {
        return Move(std::uint16_t(int(mt) | ((int(promo) - KNIGHT) << 12)
                                          | int(from) | (int(to) << 6)));
    }

    constexpr Square    from_sq()        const { return Square(data_ & 0x3F); }
    constexpr Square    to_sq()          const { return Square((data_ >> 6) & 0x3F); }
    constexpr MoveType  move_type()      const { return MoveType(data_ & (3 << 14)); }
    constexpr PieceType promotion_type() const { return PieceType(((data_ >> 12) & 3) + KNIGHT); }

    constexpr std::uint16_t raw() const { return data_; }
    constexpr bool operator==(const Move& m) const { return data_ == m.data_; }
    constexpr bool operator!=(const Move& m) const { return data_ != m.data_; }

private:
    std::uint16_t data_ = 0;
};

// "e2e4", "e7e8q", "e1g1" (castling is printed as the king move)
std::string to_uci(Move m);
std::string square_name(Square s);

}  // namespace chess
