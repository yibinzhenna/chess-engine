#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "movegen.hpp"
#include "perft.hpp"
#include "search.hpp"

using namespace chess;

namespace {

constexpr const char* ENGINE_NAME = "Scaffold 0.2";

// The difficulty the UCI loop plays at, set via `setoption name Difficulty`.
Difficulty g_difficulty = Difficulty::MagnusCarlsen;

// UCI sends moves as plain coordinates ("e2e4", "e7e8q") with no indication of
// whether they are captures, castles or en passant. Rather than re-deriving
// that, generate the legal moves and find the one that matches.
Move parse_move(Position& pos, const std::string& str) {
    MoveList list;
    generate_legal(pos, list);
    for (Move m : list)
        if (to_uci(m) == str) return m;
    return Move();
}

// position [startpos | fen <six fields>] [moves <m1> <m2> ...]
//
// The StateInfo objects for played moves must stay alive as long as the
// position does, so they live in a vector that is reset with each command.
void cmd_position(Position& pos, std::istringstream& is,
                  std::vector<StateInfo>& states) {
    std::string token;
    is >> token;

    if (token == "startpos") {
        pos.set(START_FEN);
        is >> token;                       // consume "moves" if present
    } else if (token == "fen") {
        std::string fen;
        while (is >> token && token != "moves") fen += token + " ";
        pos.set(fen);
    } else {
        return;
    }

    states.clear();
    states.reserve(512);

    if (token != "moves") return;

    while (is >> token) {
        const Move m = parse_move(pos, token);
        if (m == Move()) break;            // illegal or malformed: stop here

        // A reallocation here would dangle every StateInfo::previous pointer,
        // so refuse to grow past the reserved capacity instead.
        if (states.size() == states.capacity()) break;
        states.emplace_back();
        pos.do_move(m, states.back());
    }
}

// setoption name <name> value <value>
// Both name and value may contain spaces ("Magnus Carlsen"), so each is
// collected token by token until the next keyword.
void cmd_setoption(std::istringstream& is) {
    std::string token, name, value;

    is >> token;                                   // "name"
    while (is >> token && token != "value")
        name += (name.empty() ? "" : " ") + token;
    while (is >> token)
        value += (value.empty() ? "" : " ") + token;

    if (name == "Difficulty") {
        Difficulty d;
        if (difficulty_from_string(value, d)) {
            g_difficulty = d;
            std::printf("info string Difficulty set to %s\n", difficulty_name(d));
        } else {
            std::printf("info string unknown Difficulty '%s'\n", value.c_str());
        }
    }
}

void cmd_go(Position& pos, std::istringstream& is) {
    SearchLimits limits;
    limits.difficulty = g_difficulty;

    std::string token;
    while (is >> token) {
        if      (token == "depth")    is >> limits.depth;
        else if (token == "movetime") is >> limits.movetimeMs;
        else if (token == "wtime")    is >> limits.time[WHITE];
        else if (token == "btime")    is >> limits.time[BLACK];
        else if (token == "winc")     is >> limits.inc[WHITE];
        else if (token == "binc")     is >> limits.inc[BLACK];
        else if (token == "infinite") limits.depth = 64;
        else if (token == "perft") {
            int d = 1;
            is >> d;
            perft_divide(pos, d);
            return;
        }
    }
    // Never search forever by default when no clock was given.
    if (limits.movetimeMs == 0 && limits.time[WHITE] == 0 && limits.time[BLACK] == 0
        && limits.depth == 64)
        limits.movetimeMs = 3000;

    const Move best = search_best_move(pos, limits);
    std::printf("bestmove %s\n", best == Move() ? "0000" : to_uci(best).c_str());
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Engine-vs-engine matches
//
// This is how a difficulty ladder gets verified: if Advanced does not beat
// Beginner convincingly, the presets are wrong. It is a sanity check, not a
// substitute for measuring Elo against a rated opponent (see TODO.md).
// ---------------------------------------------------------------------------

// Deliberately conservative: bare kings, or a lone minor, cannot force mate.
// Ignores the rarer drawn cases (same-colour bishops, KNNvK), which only
// means a few extra plies before the move cap adjudicates.
bool insufficient_material(const Position& pos) {
    if (pos.pieces(PAWN) || pos.pieces(ROOK) || pos.pieces(QUEEN)) return false;
    return popcount(pos.pieces(KNIGHT) | pos.pieces(BISHOP)) <= 1;
}

// Returns +1 if White wins, -1 if Black wins, 0 for a draw.
int play_game(Difficulty white, Difficulty black, long long movetimeMs,
              std::uint64_t seed, std::string& reason) {
    Position pos;
    std::vector<StateInfo> states;
    states.reserve(1024);

    constexpr int MAX_PLIES = 400;   // 200 moves, then adjudicate as a draw

    for (int ply = 0; ply < MAX_PLIES; ++ply) {
        MoveList legal;
        generate_legal(pos, legal);

        if (legal.empty()) {
            if (pos.in_check()) {
                reason = "checkmate";
                return pos.side_to_move() == WHITE ? -1 : +1;
            }
            reason = "stalemate";
            return 0;
        }
        if (pos.halfmove_clock() >= 100)   { reason = "fifty-move rule";        return 0; }
        if (insufficient_material(pos))    { reason = "insufficient material";  return 0; }
        if (states.size() == states.capacity()) break;

        SearchLimits limits;
        limits.difficulty = (pos.side_to_move() == WHITE) ? white : black;
        limits.movetimeMs = movetimeMs;
        limits.silent     = true;
        limits.seed       = seed + std::uint64_t(ply);   // reproducible per game

        const Move m = search_best_move(pos, limits);
        if (m == Move()) break;

        states.emplace_back();
        pos.do_move(m, states.back());
    }

    reason = "move limit";
    return 0;
}

void cmd_match(Difficulty a, Difficulty b, int games, long long movetimeMs) {
    std::printf("%s vs %s  --  %d games, %lld ms/move\n\n",
                difficulty_name(a), difficulty_name(b), games, movetimeMs);

    int aWins = 0, bWins = 0, draws = 0;

    for (int g = 0; g < games; ++g) {
        // Alternate colors so neither side gets the first-move advantage twice.
        const bool aIsWhite = (g % 2 == 0);
        const Difficulty white = aIsWhite ? a : b;
        const Difficulty black = aIsWhite ? b : a;

        std::string reason;
        const int result = play_game(white, black, movetimeMs,
                                     std::uint64_t(g) * 1000003ULL + 1, reason);

        const char* winner;
        if (result == 0) {
            ++draws;
            winner = "draw";
        } else {
            const bool whiteWon = (result > 0);
            const bool aWon     = (whiteWon == aIsWhite);
            if (aWon) { ++aWins; winner = difficulty_name(a); }
            else      { ++bWins; winner = difficulty_name(b); }
        }

        std::printf("  game %2d  %-14s as white  ->  %-14s (%s)\n",
                    g + 1, difficulty_name(white), winner, reason.c_str());
        std::fflush(stdout);
    }

    const double score = (aWins + 0.5 * draws) / double(games);
    std::printf("\n%s %d - %d %s  (%d draws)   score %.1f%%\n",
                difficulty_name(a), aWins, bWins, difficulty_name(b), draws,
                score * 100.0);
}

void uci_loop() {
    Position pos;
    std::vector<StateInfo> states;
    states.reserve(512);
    std::string line;

    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string token;
        is >> token;

        if (token == "uci") {
            std::printf("id name %s\nid author you\n", ENGINE_NAME);
            std::printf("option name Difficulty type combo default Magnus Carlsen"
                        " var Beginner var Intermediate var Advanced var Magnus Carlsen\n");
            std::printf("uciok\n");
        } else if (token == "isready") {
            std::printf("readyok\n");
        } else if (token == "ucinewgame") {
            pos.set(START_FEN);
            states.clear();
        } else if (token == "setoption") {
            cmd_setoption(is);
        } else if (token == "position") {
            cmd_position(pos, is, states);
        } else if (token == "go") {
            cmd_go(pos, is);
        } else if (token == "difficulty") {          // non-standard convenience
            std::string rest, word;
            while (is >> word) rest += (rest.empty() ? "" : " ") + word;
            Difficulty d;
            if (rest.empty())
                std::printf("difficulty: %s\n", difficulty_name(g_difficulty));
            else if (difficulty_from_string(rest, d)) {
                g_difficulty = d;
                std::printf("difficulty: %s\n", difficulty_name(d));
            } else {
                std::printf("unknown difficulty '%s'\n", rest.c_str());
            }
        } else if (token == "d") {                   // non-standard, handy
            std::printf("%s", pos.to_string().c_str());
        } else if (token == "eval") {
            std::printf("eval: %d cp (side to move)\n", evaluate(pos));
        } else if (token == "quit" || token == "stop") {
            if (token == "quit") break;
        }
        std::fflush(stdout);
    }
}

void print_usage() {
    std::printf(
        "%s\n\n"
        "  chess                      start the UCI loop (what a chess GUI talks to)\n"
        "  chess test [depth]         run the perft suite (default depth 4)\n"
        "  chess perft <d> [fen]      node count at depth d\n"
        "  chess divide <d> [fen]     per-root-move node counts\n"
        "  chess bench                quick search sanity check\n"
        "  chess match <a> <b> [games] [ms]\n"
        "                             play two difficulties against each other\n"
        "\n"
        "Difficulties: beginner, intermediate, advanced, \"magnus carlsen\"\n",
        ENGINE_NAME);
}

}  // namespace

int main(int argc, char** argv) {
    chess::init();          // build the attack tables once, before anything else

    if (argc < 2) {
        uci_loop();
        return 0;
    }

    const std::string cmd = argv[1];

    if (cmd == "test") {
        const int depth = (argc > 2) ? std::atoi(argv[2]) : 4;
        return run_perft_suite(depth) ? 0 : 1;
    }

    if (cmd == "perft" || cmd == "divide") {
        const int depth = (argc > 2) ? std::atoi(argv[2]) : 1;
        std::string fen = START_FEN;
        if (argc > 3) {
            fen.clear();
            for (int i = 3; i < argc; ++i) fen += std::string(argv[i]) + " ";
        }
        Position pos;
        pos.set(fen);
        std::printf("%s", pos.to_string().c_str());

        if (cmd == "divide") {
            perft_divide(pos, depth);
        } else {
            const std::uint64_t n = perft(pos, depth);
            std::printf("perft(%d) = %llu\n", depth, (unsigned long long)n);
        }
        return 0;
    }

    if (cmd == "match") {
        if (argc < 4) {
            std::printf("usage: chess match <difficulty> <difficulty> [games] [ms/move]\n");
            return 1;
        }
        Difficulty a, b;
        if (!difficulty_from_string(argv[2], a) || !difficulty_from_string(argv[3], b)) {
            std::printf("unknown difficulty; try beginner, intermediate, advanced, magnus\n");
            return 1;
        }
        const int       games = (argc > 4) ? std::atoi(argv[4]) : 10;
        const long long ms    = (argc > 5) ? std::atoll(argv[5]) : 100;
        cmd_match(a, b, games, ms);
        return 0;
    }

    if (cmd == "bench") {
        Position pos;
        SearchLimits limits;
        limits.depth = 6;
        const Move best = search_best_move(pos, limits);
        std::printf("bestmove %s\n", to_uci(best).c_str());
        return 0;
    }

    print_usage();
    return 0;
}
