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

constexpr const char* ENGINE_NAME = "Scaffold 0.1";

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

void cmd_go(Position& pos, std::istringstream& is) {
    SearchLimits limits;
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
            std::printf("id name %s\nid author you\nuciok\n", ENGINE_NAME);
        } else if (token == "isready") {
            std::printf("readyok\n");
        } else if (token == "ucinewgame") {
            pos.set(START_FEN);
            states.clear();
        } else if (token == "position") {
            cmd_position(pos, is, states);
        } else if (token == "go") {
            cmd_go(pos, is);
        } else if (token == "d") {                      // non-standard, handy
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
        "  chess                 start the UCI loop (what a chess GUI talks to)\n"
        "  chess test [depth]    run the perft suite (default depth 4)\n"
        "  chess perft <d> [fen] node count at depth d\n"
        "  chess divide <d> [fen] per-root-move node counts\n"
        "  chess bench           quick search sanity check from the start position\n",
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
