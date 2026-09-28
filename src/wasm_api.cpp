// ---------------------------------------------------------------------------
// WebAssembly entry points.
//
// A flat C API over the engine, because JavaScript cannot call C++ methods
// directly. Everything is kept in one module-global game so the browser side
// stays a thin controller: it asks for legal moves, plays one, asks the engine
// to reply, and renders whatever comes back.
//
// Strings are returned as pointers into a static buffer. The JS caller must
// copy (UTF8ToString does) before the next call overwrites it.
// ---------------------------------------------------------------------------

#include <memory>
#include <string>
#include <vector>

#include "movegen.hpp"
#include "search.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT extern "C"
#endif

using namespace chess;

namespace {

Position g_pos;

// StateInfo objects form a linked list through `previous`, so their addresses
// must stay put for the lifetime of the game. unique_ptr gives that guarantee;
// a plain vector would dangle every back-pointer on reallocation.
std::vector<std::unique_ptr<StateInfo>> g_states;
std::vector<Move> g_history;

std::string g_buffer;   // backing store for every string this API returns

const char* ret(const std::string& s) {
    g_buffer = s;
    return g_buffer.c_str();
}

Difficulty difficulty_from_int(int d) {
    switch (d) {
        case 0:  return Difficulty::Beginner;
        case 1:  return Difficulty::Intermediate;
        case 2:  return Difficulty::Advanced;
        default: return Difficulty::MagnusCarlsen;
    }
}

// Mirrors the adjudication used by the `match` command. Deliberately
// conservative: it ignores the rarer drawn endings (same-colour bishops).
bool insufficient_material(const Position& pos) {
    if (pos.pieces(PAWN) || pos.pieces(ROOK) || pos.pieces(QUEEN)) return false;
    return popcount(pos.pieces(KNIGHT) | pos.pieces(BISHOP)) <= 1;
}

void reset(const std::string& fen) {
    g_states.clear();
    g_history.clear();
    g_pos.set(fen);
}

}  // namespace

// --- lifecycle -------------------------------------------------------------

EXPORT void engine_init() {
    chess::init();          // build attack tables once
    reset(START_FEN);
}

EXPORT void engine_new_game() { reset(START_FEN); }

EXPORT int engine_set_fen(const char* fen) {
    reset(fen ? fen : START_FEN);
    // A position with no king for either side is malformed; catching it here
    // stops king_square() from reading an empty bitboard later.
    if (!g_pos.pieces(WHITE, KING) || !g_pos.pieces(BLACK, KING)) {
        reset(START_FEN);
        return 0;
    }
    return 1;
}

// --- queries ---------------------------------------------------------------

EXPORT const char* engine_fen() { return ret(g_pos.fen()); }

// 64 characters in reading order (a8..h8, a7..h7, ... a1..h1), '.' for empty.
// Far easier for the UI to consume than re-parsing FEN on every render.
EXPORT const char* engine_board() {
    static const char* PIECE_CHARS = " PNBRQK  pnbrqk";
    std::string out;
    out.reserve(64);
    for (int r = RANK_8; r >= RANK_1; --r)
        for (int f = FILE_A; f <= FILE_H; ++f) {
            const Piece pc = g_pos.piece_on(make_square(File(f), Rank(r)));
            out += (pc == NO_PIECE) ? '.' : PIECE_CHARS[pc];
        }
    return ret(out);
}

// Space-separated UCI moves, e.g. "e2e4 e2e3 g1f3". The UI uses this both to
// highlight destinations and to validate a drag before sending it.
EXPORT const char* engine_legal_moves() {
    MoveList list;
    generate_legal(g_pos, list);

    std::string out;
    for (Move m : list) {
        if (!out.empty()) out += ' ';
        out += to_uci(m);
    }
    return ret(out);
}

EXPORT int engine_side_to_move() { return g_pos.side_to_move() == WHITE ? 0 : 1; }
EXPORT int engine_in_check()     { return g_pos.in_check() ? 1 : 0; }
EXPORT int engine_move_count()   { return int(g_history.size()); }

// 0 ongoing, 1 checkmate (side to move is mated), 2 stalemate,
// 3 fifty-move draw, 4 insufficient material.
EXPORT int engine_status() {
    MoveList list;
    generate_legal(g_pos, list);
    if (list.empty()) return g_pos.in_check() ? 1 : 2;
    if (g_pos.halfmove_clock() >= 100) return 3;
    if (insufficient_material(g_pos)) return 4;
    return 0;
}

// --- mutation --------------------------------------------------------------

EXPORT int engine_make_move(const char* uci) {
    if (!uci) return 0;
    const std::string want(uci);

    MoveList list;
    generate_legal(g_pos, list);
    for (Move m : list) {
        if (to_uci(m) != want) continue;
        g_states.push_back(std::make_unique<StateInfo>());
        g_pos.do_move(m, *g_states.back());
        g_history.push_back(m);
        return 1;
    }
    return 0;   // not legal in this position
}

EXPORT int engine_undo() {
    if (g_history.empty()) return 0;
    g_pos.undo_move(g_history.back());
    g_history.pop_back();
    g_states.pop_back();
    return 1;
}

// --- search ----------------------------------------------------------------

// Returns the chosen move in UCI, or "" if the game is already over.
// `silent` is set so the engine's UCI info lines do not spam the browser
// console on every move.
EXPORT const char* engine_best_move(int difficulty, int movetimeMs) {
    SearchLimits limits;
    limits.difficulty = difficulty_from_int(difficulty);
    limits.movetimeMs = movetimeMs > 0 ? movetimeMs : 300;
    limits.silent     = true;

    const Move m = search_best_move(g_pos, limits);
    return ret(m == Move() ? std::string() : to_uci(m));
}

// Convenience for the worker: search, play it, and report what was played.
EXPORT const char* engine_play_best(int difficulty, int movetimeMs) {
    const std::string uci = engine_best_move(difficulty, movetimeMs);
    if (uci.empty()) return ret(std::string());
    engine_make_move(uci.c_str());
    return ret(uci);
}

EXPORT int engine_evaluate() { return evaluate(g_pos); }
