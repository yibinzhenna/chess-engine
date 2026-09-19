#include "position.hpp"

#include <array>
#include <cctype>
#include <cstring>
#include <sstream>

namespace chess {

namespace {

constexpr const char* PIECE_CHARS = " PNBRQK  pnbrqk";

// Removing castling rights on a move is a pure function of the from/to
// squares: a king leaving e1 kills both white rights, a rook leaving (or being
// captured on) a1/h1 kills one. AND-ing both masks handles every case at once.
constexpr std::array<int, SQUARE_NB> make_castling_masks() {
    std::array<int, SQUARE_NB> m{};
    for (auto& v : m) v = ANY_CASTLING;
    m[SQ_E1] &= ~(WHITE_OO | WHITE_OOO);
    m[SQ_H1] &= ~WHITE_OO;
    m[SQ_A1] &= ~WHITE_OOO;
    m[SQ_E8] &= ~(BLACK_OO | BLACK_OOO);
    m[SQ_H8] &= ~BLACK_OO;
    m[SQ_A8] &= ~BLACK_OOO;
    return m;
}

constexpr auto CastlingMask = make_castling_masks();

}  // namespace

std::string square_name(Square s) {
    if (!is_ok(s)) return "-";
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string to_uci(Move m) {
    std::string out = square_name(m.from_sq()) + square_name(m.to_sq());
    if (m.move_type() == PROMOTION)
        out += " pnbrqk"[m.promotion_type()];
    return out;
}

// ---------------------------------------------------------------------------
// Piece placement helpers. Every mutation must keep board_, byType_ and
// byColor_ in agreement -- that invariant is the whole ballgame.
// ---------------------------------------------------------------------------

void Position::put_piece(Piece pc, Square s) {
    const Bitboard b = square_bb(s);
    board_[s] = pc;
    byType_[NO_PIECE_TYPE] |= b;
    byType_[type_of(pc)]   |= b;
    byColor_[color_of(pc)] |= b;
}

void Position::remove_piece(Square s) {
    const Piece pc = board_[s];
    const Bitboard b = square_bb(s);
    byType_[NO_PIECE_TYPE] ^= b;
    byType_[type_of(pc)]   ^= b;
    byColor_[color_of(pc)] ^= b;
    board_[s] = NO_PIECE;
}

void Position::move_piece(Square from, Square to) {
    const Piece pc = board_[from];
    const Bitboard fromTo = square_bb(from) | square_bb(to);
    byType_[NO_PIECE_TYPE] ^= fromTo;
    byType_[type_of(pc)]   ^= fromTo;
    byColor_[color_of(pc)] ^= fromTo;
    board_[from] = NO_PIECE;
    board_[to]   = pc;
}

// ---------------------------------------------------------------------------
// FEN
// ---------------------------------------------------------------------------

void Position::set(const std::string& fen) {
    for (auto& b : byType_)  b = 0;
    for (auto& b : byColor_) b = 0;
    for (auto& p : board_)   p = NO_PIECE;
    rootState_ = StateInfo{};
    st_ = &rootState_;

    std::istringstream ss(fen);
    std::string placement, stm, castling, ep;
    ss >> placement >> stm >> castling >> ep;

    int rank = RANK_8, file = FILE_A;
    for (char c : placement) {
        if (c == '/') {
            --rank;
            file = FILE_A;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            file += c - '0';
        } else {
            const char* p = std::strchr(PIECE_CHARS, c);
            if (p && *p && file <= FILE_H && rank >= RANK_1) {
                put_piece(Piece(p - PIECE_CHARS), make_square(File(file), Rank(rank)));
                ++file;
            }
        }
    }

    sideToMove_ = (stm == "b") ? BLACK : WHITE;

    st_->castlingRights = NO_CASTLING;
    for (char c : castling) {
        switch (c) {
            case 'K': st_->castlingRights |= WHITE_OO;  break;
            case 'Q': st_->castlingRights |= WHITE_OOO; break;
            case 'k': st_->castlingRights |= BLACK_OO;  break;
            case 'q': st_->castlingRights |= BLACK_OOO; break;
            default:  break;
        }
    }

    st_->epSquare = SQ_NONE;
    if (ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' && ep[1] >= '1' && ep[1] <= '8')
        st_->epSquare = make_square(File(ep[0] - 'a'), Rank(ep[1] - '1'));

    int halfmove = 0, fullmove = 1;
    if (!(ss >> halfmove)) halfmove = 0;
    if (!(ss >> fullmove)) fullmove = 1;
    st_->halfmoveClock = halfmove;
    fullmoveNumber_    = fullmove;
}

std::string Position::fen() const {
    std::ostringstream ss;
    for (int r = RANK_8; r >= RANK_1; --r) {
        int emptyRun = 0;
        for (int f = FILE_A; f <= FILE_H; ++f) {
            const Piece pc = piece_on(make_square(File(f), Rank(r)));
            if (pc == NO_PIECE) {
                ++emptyRun;
                continue;
            }
            if (emptyRun) {
                ss << emptyRun;
                emptyRun = 0;
            }
            ss << PIECE_CHARS[pc];
        }
        if (emptyRun) ss << emptyRun;
        if (r != RANK_1) ss << '/';
    }
    ss << (sideToMove_ == WHITE ? " w " : " b ");

    const int cr = st_->castlingRights;
    if (!cr) {
        ss << '-';
    } else {
        if (cr & WHITE_OO)  ss << 'K';
        if (cr & WHITE_OOO) ss << 'Q';
        if (cr & BLACK_OO)  ss << 'k';
        if (cr & BLACK_OOO) ss << 'q';
    }
    ss << ' ' << square_name(st_->epSquare)
       << ' ' << st_->halfmoveClock
       << ' ' << fullmoveNumber_;
    return ss.str();
}

std::string Position::to_string() const {
    std::string out = "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = RANK_8; r >= RANK_1; --r) {
        for (int f = FILE_A; f <= FILE_H; ++f) {
            out += " | ";
            out += PIECE_CHARS[piece_on(make_square(File(f), Rank(r)))];
        }
        out += " | " + std::to_string(r + 1) + "\n +---+---+---+---+---+---+---+---+\n";
    }
    out += "   a   b   c   d   e   f   g   h\n\nFen: " + fen() + "\n";
    return out;
}

// ---------------------------------------------------------------------------
// Attack detection
//
// The trick: instead of asking "what does every enemy piece attack?", stand on
// the target square and look outward as if you were each piece type in turn.
// A knight attacks s iff a knight placed on s would attack that knight.
// ---------------------------------------------------------------------------

bool Position::attacked_by(Square s, Color by, Bitboard occupied) const {
    // Pawn attacks are not symmetric, so flip the color: the squares from
    // which a `by` pawn hits s are exactly those a `~by` pawn on s would hit.
    if (PawnAttacks[~by][s]      & pieces(by, PAWN))   return true;
    if (PseudoAttacks[KNIGHT][s] & pieces(by, KNIGHT)) return true;
    if (PseudoAttacks[KING][s]   & pieces(by, KING))   return true;

    const Bitboard bishopsQueens = pieces(by, BISHOP) | pieces(by, QUEEN);
    if (sliding_attacks(s, occupied, true) & bishopsQueens) return true;

    const Bitboard rooksQueens = pieces(by, ROOK) | pieces(by, QUEEN);
    if (sliding_attacks(s, occupied, false) & rooksQueens) return true;

    return false;
}

// ---------------------------------------------------------------------------
// do / undo
// ---------------------------------------------------------------------------

void Position::do_move(Move m, StateInfo& newSt) {
    newSt.previous       = st_;
    newSt.castlingRights = st_->castlingRights;
    newSt.halfmoveClock  = st_->halfmoveClock + 1;
    newSt.epSquare       = SQ_NONE;   // an ep right lasts exactly one ply
    newSt.capturedPiece  = NO_PIECE;
    st_ = &newSt;

    const Color     us   = sideToMove_;
    const Square    from = m.from_sq();
    const Square    to   = m.to_sq();
    const MoveType  mt   = m.move_type();
    const PieceType pt   = type_of(piece_on(from));

    if (mt == CASTLING) {
        // The king moves two squares; the rook jumps to the square it crossed.
        const bool   kingside = to > from;
        const Square rookFrom = make_square(kingside ? FILE_H : FILE_A, rank_of(from));
        const Square rookTo   = make_square(kingside ? FILE_F : FILE_D, rank_of(from));
        move_piece(from, to);
        move_piece(rookFrom, rookTo);
    } else {
        // For en passant the captured pawn is NOT on the destination square.
        const Square capSq = (mt == EN_PASSANT)
                           ? Square(int(to) - (us == WHITE ? 8 : -8))
                           : to;

        const Piece captured = piece_on(capSq);
        if (captured != NO_PIECE) {
            st_->capturedPiece = captured;
            st_->halfmoveClock = 0;
            remove_piece(capSq);
        }

        move_piece(from, to);

        if (mt == PROMOTION) {
            remove_piece(to);
            put_piece(make_piece(us, m.promotion_type()), to);
        }

        if (pt == PAWN) {
            st_->halfmoveClock = 0;
            if ((int(from) ^ int(to)) == 16)            // a double push
                st_->epSquare = Square((int(from) + int(to)) / 2);
        }
    }

    st_->castlingRights &= CastlingMask[from] & CastlingMask[to];

    sideToMove_ = ~us;
    if (us == BLACK) ++fullmoveNumber_;
}

void Position::undo_move(Move m) {
    sideToMove_ = ~sideToMove_;            // back to the side that moved

    const Color    us   = sideToMove_;
    const Square   from = m.from_sq();
    const Square   to   = m.to_sq();
    const MoveType mt   = m.move_type();

    if (mt == CASTLING) {
        const bool   kingside = to > from;
        const Square rookFrom = make_square(kingside ? FILE_H : FILE_A, rank_of(from));
        const Square rookTo   = make_square(kingside ? FILE_F : FILE_D, rank_of(from));
        move_piece(to, from);
        move_piece(rookTo, rookFrom);
    } else {
        if (mt == PROMOTION) {             // put the pawn back before moving it
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);

        if (st_->capturedPiece != NO_PIECE) {
            const Square capSq = (mt == EN_PASSANT)
                               ? Square(int(to) - (us == WHITE ? 8 : -8))
                               : to;
            put_piece(st_->capturedPiece, capSq);
        }
    }

    if (us == BLACK) --fullmoveNumber_;
    st_ = st_->previous;                   // pop the irreversible state
}

}  // namespace chess
