#include "search.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

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
    long long   allottedMs    = 0;
    std::uint64_t nodes       = 0;
    bool        stopped       = false;
    bool        useQuiescence = true;

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

    // Skipping quiescence is the Beginner handicap. Leaf positions then get
    // scored mid-exchange, with every pending capture ignored, so the bot both
    // walks into losing trades and shies away from good ones -- misjudging
    // material the way a real beginner does, with no artificial randomness.
    if (depth <= 0)
        return ctx.useQuiescence ? qsearch(pos, alpha, beta, ctx) : evaluate(pos);

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

// ---------------------------------------------------------------------------
// Difficulty presets
//
// Each level layers handicaps rather than turning a single dial. Elo figures
// are design targets, NOT measured results -- see TODO.md for the calibration
// plan. Re-measure after any change that affects playing strength.
// ---------------------------------------------------------------------------

Skill skill_for(Difficulty d) {
    switch (d) {
        case Difficulty::Beginner:       // target ~600-800
            // No quiescence, so it hangs pieces to recaptures; wide noise and
            // frequent blunders on top of a two-ply horizon.
            return {"Beginner",      2,  200, 150, 35, 6, false};

        case Difficulty::Intermediate:   // target ~1200-1400
            return {"Intermediate",  4,  500,  60, 15, 4, true};

        case Difficulty::Advanced:       // target ~1700-1900
            return {"Advanced",      7, 1500,  20,  5, 3, true};

        case Difficulty::MagnusCarlsen:  // full strength, no handicap at all
        default:
            return {"Magnus Carlsen", 64,  0,   0,  0, 1, true};
    }
}

const char* difficulty_name(Difficulty d) { return skill_for(d).name; }

bool difficulty_from_string(const std::string& s, Difficulty& out) {
    std::string t;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c)))
            t += char(std::tolower(static_cast<unsigned char>(c)));

    if (t == "beginner" || t == "easy" || t == "0") {
        out = Difficulty::Beginner;       return true;
    }
    if (t == "intermediate" || t == "medium" || t == "1") {
        out = Difficulty::Intermediate;   return true;
    }
    if (t == "advanced" || t == "experienced" || t == "hard" || t == "2") {
        out = Difficulty::Advanced;       return true;
    }
    if (t == "magnuscarlsen" || t == "magnus" || t == "carlsen" || t == "max" || t == "3") {
        out = Difficulty::MagnusCarlsen;  return true;
    }
    return false;
}

namespace {

struct RootMove {
    Move move;
    int  score = -VALUE_INF;
};

// Decide which of the scored root moves to actually play. `moves` arrives
// sorted best-first and is taken by value because this reorders it.
Move pick_move(std::vector<RootMove> moves, const Skill& skill, std::mt19937_64& rng) {
    if (moves.size() == 1) return moves[0].move;

    const bool handicapped = skill.evalNoise > 0 || skill.blunderPercent > 0;
    if (!handicapped) return moves[0].move;

    // A found forced mate is always played. A real beginner would sometimes
    // miss one, but an engine that discards mates produces games that never
    // end, which is worse than the small loss of realism.
    if (moves[0].score >= VALUE_MATE - MAX_PLY) return moves[0].move;

    // Noise models fuzzy positional judgment: the bot's ranking of quiet moves
    // stops matching its own evaluation. Mate scores are left alone so the
    // engine never shuffles a forced loss into looking acceptable.
    if (skill.evalNoise > 0) {
        std::uniform_int_distribution<int> noise(-skill.evalNoise, skill.evalNoise);
        for (RootMove& rm : moves)
            if (std::abs(rm.score) < VALUE_MATE - MAX_PLY) rm.score += noise(rng);

        std::stable_sort(moves.begin(), moves.end(),
                         [](const RootMove& a, const RootMove& b) { return a.score > b.score; });
    }

    // An outright blunder: deliberately pass over the move it now thinks is
    // best. Choosing from the top N keeps the mistake plausible -- a uniformly
    // random legal move reads as a bot immediately.
    int pick = 0;
    const int width = std::min<int>(skill.topN, int(moves.size()));
    if (width > 1 && skill.blunderPercent > 0) {
        std::uniform_int_distribution<int> roll(1, 100);
        if (roll(rng) <= skill.blunderPercent) {
            std::uniform_int_distribution<int> choose(1, width - 1);
            pick = choose(rng);
        }
    }
    return moves[pick].move;
}

void print_info(int depth, int score, const SearchContext& ctx, Move best, long long ms) {
    // UCI distinguishes a centipawn score from a forced mate. Mate scores are
    // stored as (VALUE_MATE - ply), so the distance in plies is the difference;
    // the protocol wants it in MOVES, negative if we are the one being mated.
    char scoreStr[32];
    if (std::abs(score) >= VALUE_MATE - MAX_PLY) {
        const int plies  = VALUE_MATE - std::abs(score);
        const int mateIn = (plies + 1) / 2;
        std::snprintf(scoreStr, sizeof(scoreStr), "mate %d", score > 0 ? mateIn : -mateIn);
    } else {
        std::snprintf(scoreStr, sizeof(scoreStr), "cp %d", score);
    }

    std::printf("info depth %d score %s nodes %llu time %lld pv %s\n",
                depth, scoreStr, (unsigned long long)ctx.nodes,
                (long long)ms, to_uci(best).c_str());
    std::fflush(stdout);
}

}  // namespace

Move search_best_move(Position& pos, const SearchLimits& limits) {
    const Skill skill = skill_for(limits.difficulty);

    SearchContext ctx;
    ctx.start         = Clock::now();
    ctx.useQuiescence = skill.useQuiescence;

    if (limits.movetimeMs > 0) {
        ctx.allottedMs = limits.movetimeMs;
    } else if (limits.time[pos.side_to_move()] > 0) {
        const Color us = pos.side_to_move();
        // Crude but serviceable: spend a thirtieth of the remaining clock,
        // plus most of the increment, and never more than half of what is left.
        ctx.allottedMs = std::min(limits.time[us] / 2,
                                  limits.time[us] / 30 + limits.inc[us] * 3 / 4);
    }
    // A weaker bot should also think for less time, so it feels different to
    // play against and not merely worse.
    if (skill.maxMoveMs > 0)
        ctx.allottedMs = ctx.allottedMs > 0 ? std::min(ctx.allottedMs, skill.maxMoveMs)
                                            : skill.maxMoveMs;

    const int maxDepth = std::min(limits.depth, skill.maxDepth);

    MoveList rootList;
    generate_legal(pos, rootList);
    if (rootList.empty()) return Move();

    std::vector<RootMove> roots;
    roots.reserve(rootList.size());
    for (Move m : rootList) roots.push_back({m, -VALUE_INF});

    // Handicapped play needs an exact score for EVERY root move, not just the
    // best one -- alpha-beta returns mere bounds for the rest, which cannot be
    // ranked. Full strength keeps the narrowed window, which is faster.
    const bool needsAllScores = skill.evalNoise > 0 || skill.blunderPercent > 0;

    std::mt19937_64 rng(limits.seed ? limits.seed : std::random_device{}());

    std::vector<RootMove> completed;      // last fully-searched iteration

    for (int depth = 1; depth <= maxDepth; ++depth) {
        int alpha = -VALUE_INF;

        for (RootMove& rm : roots) {
            StateInfo st;
            pos.do_move(rm.move, st);
            rm.score = -negamax(pos, depth - 1, -VALUE_INF,
                                needsAllScores ? VALUE_INF : -alpha, 1, ctx);
            pos.undo_move(rm.move);

            if (ctx.stopped) break;
            if (rm.score > alpha) alpha = rm.score;
        }

        // A half-finished iteration can rank moves worse than the previous
        // complete one, so only accept a depth that actually finished.
        if (ctx.stopped) break;

        // Best first, which both answers "what did it find?" and orders the
        // next iteration for better pruning.
        std::stable_sort(roots.begin(), roots.end(),
                         [](const RootMove& a, const RootMove& b) { return a.score > b.score; });
        completed = roots;

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            Clock::now() - ctx.start).count();
        if (!limits.silent)
            print_info(depth, completed[0].score, ctx, completed[0].move, ms);

        if (ctx.allottedMs > 0 && ms * 2 >= ctx.allottedMs) break;  // no time for another ply
    }

    if (completed.empty()) return roots[0].move;   // stopped before depth 1 finished
    return pick_move(completed, skill, rng);
}

}  // namespace chess
