#include "search.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "movegen.hpp"

namespace chess {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int PieceValue[PIECE_TYPE_NB] = {
    0, 100, 320, 330, 500, 900, 0   // none, P, N, B, R, Q, K
};

// Piece-square tables, written the way a board looks on screen: index 0 is a8,
// index 63 is h1. A white piece on square s therefore reads index (s ^ 56),
// which flips the rank; a black piece reads s directly, giving a free mirror.
// Values are the classic "simplified evaluation" set -- crude, but enough to
// make the engine develop pieces and castle instead of shuffling.
constexpr int PST[PIECE_TYPE_NB][SQUARE_NB] = {
    {},  // NO_PIECE_TYPE
    {    // PAWN
         0,  0,  0,  0,  0,  0,  0,  0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
         5,  5, 10, 25, 25, 10,  5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5, -5,-10,  0,  0,-10, -5,  5,
         5, 10, 10,-20,-20, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0
    },
    {    // KNIGHT
       -50,-40,-30,-30,-30,-30,-40,-50,
       -40,-20,  0,  0,  0,  0,-20,-40,
       -30,  0, 10, 15, 15, 10,  0,-30,
       -30,  5, 15, 20, 20, 15,  5,-30,
       -30,  0, 15, 20, 20, 15,  0,-30,
       -30,  5, 10, 15, 15, 10,  5,-30,
       -40,-20,  0,  5,  5,  0,-20,-40,
       -50,-40,-30,-30,-30,-30,-40,-50
    },
    {    // BISHOP
       -20,-10,-10,-10,-10,-10,-10,-20,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -10,  0,  5, 10, 10,  5,  0,-10,
       -10,  5,  5, 10, 10,  5,  5,-10,
       -10,  0, 10, 10, 10, 10,  0,-10,
       -10, 10, 10, 10, 10, 10, 10,-10,
       -10,  5,  0,  0,  0,  0,  5,-10,
       -20,-10,-10,-10,-10,-10,-10,-20
    },
    {    // ROOK
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10, 10, 10, 10, 10,  5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
         0,  0,  0,  5,  5,  0,  0,  0
    },
    {    // QUEEN
       -20,-10,-10, -5, -5,-10,-10,-20,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -10,  0,  5,  5,  5,  5,  0,-10,
        -5,  0,  5,  5,  5,  5,  0, -5,
         0,  0,  5,  5,  5,  5,  0, -5,
       -10,  5,  5,  5,  5,  5,  0,-10,
       -10,  0,  5,  0,  0,  0,  0,-10,
       -20,-10,-10, -5, -5,-10,-10,-20
    },
    {    // KING (middlegame: stay home, get castled)
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -20,-30,-30,-40,-40,-30,-30,-20,
       -10,-20,-20,-20,-20,-20,-20,-10,
        20, 20,  0,  0,  0, 20, 20, 20,
        20, 30, 10,  0,  0, 10, 30, 20
    }
};

// --- search state ----------------------------------------------------------

struct SearchContext {
    Clock::time_point start;
    long long   allottedMs = 0;
    std::uint64_t nodes    = 0;
    bool        stopped    = false;

    bool out_of_time() {
        if (stopped) return true;
        // Polling the clock is not free, so only check every few thousand nodes.
        if ((nodes & 2047) == 0 && allottedMs > 0) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                Clock::now() - start).count();
            if (ms >= allottedMs) stopped = true;
        }
        return stopped;
    }
};

// Most Valuable Victim / Least Valuable Attacker. Searching PxQ before QxP
// makes alpha-beta cut off far sooner; move ordering is worth more Elo than
// almost anything else you can add at this stage.
int score_move(const Position& pos, Move m) {
    int score = 0;
    const Piece captured = pos.piece_on(m.to_sq());
    if (captured != NO_PIECE)
        score += 10 * PieceValue[type_of(captured)]
               - PieceValue[type_of(pos.piece_on(m.from_sq()))] + 10000;

    if (m.move_type() == PROMOTION) score += PieceValue[m.promotion_type()] + 9000;
    if (m.move_type() == EN_PASSANT) score += 10 * PieceValue[PAWN] + 10000;
    return score;
}

void sort_moves(const Position& pos, MoveList& list) {
    int scores[256];
    for (int i = 0; i < list.count; ++i) scores[i] = score_move(pos, list.moves[i]);

    // Insertion sort: the list is short and usually near-sorted already.
    for (int i = 1; i < list.count; ++i) {
        const Move m = list.moves[i];
        const int  s = scores[i];
        int j = i - 1;
        while (j >= 0 && scores[j] < s) {
            list.moves[j + 1] = list.moves[j];
            scores[j + 1]     = scores[j];
            --j;
        }
        list.moves[j + 1] = m;
        scores[j + 1]     = s;
    }
}

// Quiescence search: at the leaves, keep searching captures only, until the
// position is "quiet". Without this the engine happily grabs a queen with a
// pawn on the last ply and never sees the recapture -- the horizon effect.
int qsearch(Position& pos, int alpha, int beta, SearchContext& ctx) {
    ++ctx.nodes;
    if (ctx.out_of_time()) return 0;

    // Standing pat: we are not forced to capture, so the static eval is a
    // lower bound on what we can achieve.
    const int stand = evaluate(pos);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    MoveList list;
    generate_legal(pos, list);
    sort_moves(pos, list);

    for (Move m : list) {
        const bool isCapture = pos.piece_on(m.to_sq()) != NO_PIECE
                            || m.move_type() == EN_PASSANT;
        if (!isCapture && m.move_type() != PROMOTION) continue;

        StateInfo st;
        pos.do_move(m, st);
        const int score = -qsearch(pos, -beta, -alpha, ctx);
        pos.undo_move(m);

        if (ctx.stopped) return 0;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

int negamax(Position& pos, int depth, int alpha, int beta, int ply, SearchContext& ctx) {
    ++ctx.nodes;
    if (ctx.out_of_time()) return 0;

    if (depth <= 0) return qsearch(pos, alpha, beta, ctx);

    MoveList list;
    generate_legal(pos, list);

    if (list.empty()) {
        // Distinguishing checkmate from stalemate is the whole reason we need
        // the legal move list rather than a count. Scoring mate as
        // (-MATE + ply) makes the engine prefer the FASTER mate.
        return pos.in_check() ? -VALUE_MATE + ply : 0;
    }

    if (pos.halfmove_clock() >= 100) return 0;   // fifty-move draw

    sort_moves(pos, list);

    for (Move m : list) {
        StateInfo st;
        pos.do_move(m, st);
        const int score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, ctx);
        pos.undo_move(m);

        if (ctx.stopped) return 0;
        if (score >= beta) return beta;          // opponent won't allow this line
        if (score > alpha) alpha = score;
    }
    return alpha;
}

}  // namespace

// ---------------------------------------------------------------------------

int evaluate(const Position& pos) {
    int score = 0;
    for (int pt = PAWN; pt <= KING; ++pt) {
        Bitboard white = pos.pieces(WHITE, PieceType(pt));
        while (white) {
            const Square s = pop_lsb(white);
            score += PieceValue[pt] + PST[pt][int(s) ^ 56];
        }
        Bitboard black = pos.pieces(BLACK, PieceType(pt));
        while (black) {
            const Square s = pop_lsb(black);
            score -= PieceValue[pt] + PST[pt][int(s)];
        }
    }
    // Negamax wants the score from the mover's point of view.
    return pos.side_to_move() == WHITE ? score : -score;
}

Move search_best_move(Position& pos, const SearchLimits& limits) {
    SearchContext ctx;
    ctx.start = Clock::now();

    if (limits.movetimeMs > 0) {
        ctx.allottedMs = limits.movetimeMs;
    } else if (limits.time[pos.side_to_move()] > 0) {
        const Color us = pos.side_to_move();
        // Crude but serviceable: spend a thirtieth of the remaining clock,
        // plus most of the increment, and never more than half of what is left.
        ctx.allottedMs = std::min(limits.time[us] / 2,
                                  limits.time[us] / 30 + limits.inc[us] * 3 / 4);
    }

    MoveList root;
    generate_legal(pos, root);
    if (root.empty()) return Move();

    Move best = root[0];

    for (int depth = 1; depth <= limits.depth; ++depth) {
        int  alpha = -VALUE_INF;
        Move bestThisIteration = root[0];

        sort_moves(pos, root);
        for (Move m : root) {
            StateInfo st;
            pos.do_move(m, st);
            const int score = -negamax(pos, depth - 1, -VALUE_INF, -alpha, 1, ctx);
            pos.undo_move(m);

            if (ctx.stopped) break;
            if (score > alpha) {
                alpha = score;
                bestThisIteration = m;
            }
        }

        // A half-finished iteration can return a worse move than the previous
        // complete one, so only accept it if the depth actually completed.
        if (ctx.stopped) break;
        best = bestThisIteration;

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            Clock::now() - ctx.start).count();
        std::printf("info depth %d score cp %d nodes %llu time %lld pv %s\n",
                    depth, alpha, (unsigned long long)ctx.nodes,
                    (long long)ms, to_uci(best).c_str());
        std::fflush(stdout);

        if (ctx.allottedMs > 0 && ms * 2 >= ctx.allottedMs) break;  // no time for another ply
    }

    return best;
}

}  // namespace chess
