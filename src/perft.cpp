#include "perft.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "movegen.hpp"

namespace chess {

std::uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;

    MoveList list;
    generate_legal(pos, list);

    // Bulk counting: at depth 1 the number of legal moves IS the node count,
    // so skip a whole ply of make/unmake. Worth roughly a 3x speedup.
    if (depth == 1) return std::uint64_t(list.size());

    std::uint64_t nodes = 0;
    for (Move m : list) {
        StateInfo st;
        pos.do_move(m, st);
        nodes += perft(pos, depth - 1);
        pos.undo_move(m);
    }
    return nodes;
}

std::uint64_t perft_divide(Position& pos, int depth) {
    if (depth <= 0) return 1;

    MoveList list;
    generate_legal(pos, list);

    std::uint64_t total = 0;
    for (Move m : list) {
        StateInfo st;
        pos.do_move(m, st);
        const std::uint64_t n = perft(pos, depth - 1);
        pos.undo_move(m);
        std::printf("%s: %llu\n", to_uci(m).c_str(), (unsigned long long)n);
        total += n;
    }
    std::printf("\nNodes searched: %llu\n", (unsigned long long)total);
    return total;
}

namespace {

struct PerftCase {
    const char* name;
    const char* fen;
    std::vector<std::uint64_t> expected;   // index 0 == depth 1
};

// The standard suite from the Chess Programming Wiki. Between them these six
// positions exercise every rule that movegen tends to get wrong: en passant
// discovered check, promotion while in check, castling through attacked
// squares, and pinned-piece edge cases.
const std::vector<PerftCase>& cases() {
    static const std::vector<PerftCase> data = {
        {"Startpos",
         "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
         {20, 400, 8902, 197281, 4865609, 119060324}},

        {"Kiwipete",
         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         {48, 2039, 97862, 4085603, 193690690}},

        {"Position 3",
         "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         {14, 191, 2812, 43238, 674624, 11030083}},

        {"Position 4",
         "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         {6, 264, 9467, 422333, 15833292}},

        {"Position 5",
         "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         {44, 1486, 62379, 2103487, 89941194}},

        {"Position 6",
         "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         {46, 2079, 89890, 3894594, 164075551}},
    };
    return data;
}

}  // namespace

bool run_perft_suite(int maxDepth) {
    using Clock = std::chrono::steady_clock;

    bool allPassed = true;
    std::uint64_t totalNodes = 0;
    const auto start = Clock::now();

    for (const PerftCase& c : cases()) {
        std::printf("%-12s %s\n", c.name, c.fen);
        Position pos;
        pos.set(c.fen);

        const int depths = std::min<int>(maxDepth, int(c.expected.size()));
        for (int d = 1; d <= depths; ++d) {
            const auto t0 = Clock::now();
            const std::uint64_t got = perft(pos, d);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                Clock::now() - t0).count();

            const std::uint64_t want = c.expected[d - 1];
            const bool ok = (got == want);
            allPassed &= ok;
            totalNodes += got;

            std::printf("  depth %d  %14llu  %s", d, (unsigned long long)got,
                        ok ? "ok" : "FAIL");
            if (!ok) std::printf("  (expected %llu)", (unsigned long long)want);
            std::printf("   [%lld ms]\n", (long long)ms);

            if (!ok) break;   // deeper counts are meaningless once one is wrong
        }
        std::printf("\n");
    }

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             Clock::now() - start).count();
    const double nps = totalMs ? double(totalNodes) / double(totalMs) * 1000.0 : 0.0;

    std::printf("%s  --  %llu nodes in %lld ms (%.0f nodes/sec)\n",
                allPassed ? "ALL PASSED" : "FAILURES PRESENT",
                (unsigned long long)totalNodes, (long long)totalMs, nps);
    return allPassed;
}

}  // namespace chess
