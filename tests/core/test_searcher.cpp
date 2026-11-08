#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stop_token>
#include <vector>

#include "core/interface.hpp"
#include "core/pdb.hpp"
#include "core/searcher.hpp"

// -- Toy domain: state is an int on a line, moves are +-1 ------------------
// Independent of CubeState: exercises the generic searcher directly.
namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                       \
  do {                                                         \
    if (!(cond)) {                                             \
      std::cerr << "FAIL: " << msg << "\n";                    \
      ++g_failures;                                            \
    } else {                                                   \
      std::cout << "ok: " << msg << "\n";                      \
    }                                                          \
  } while (0)

using rubik::SolveError;
using rubik::core::IdaSearcher;
using rubik::core::bfsDb;
using rubik::core::pdbLookup;

auto applyLine = [](int s, int move) -> int {
  // move 0 -> +1, 1 -> -1; clamp to [0, 8] to keep the graph finite.
  int n = s + (move == 0 ? 1 : -1);
  if (n < 0) n = 0;
  if (n > 8) n = 8;
  return n;
};

auto heuristicTo(int target) {
  return [target](int s) {
    int d = s - target;
    return d < 0 ? -d : d;
  };
}

auto isGoal(int target) {
  return [target](int s) { return s == target; };
}

std::vector<int> replay(int start, const std::vector<int>& path) {
  std::vector<int> trace{start};
  int s = start;
  for (int m : path) {
    s = applyLine(s, m);
    trace.push_back(s);
  }
  return trace;
}

void testPdbLookup() {
  std::vector<std::uint8_t> db{255, 0, 3, 255, 7};
  CHECK(pdbLookup(db, 0) == 0, "pdbLookup 255 -> 0");
  CHECK(pdbLookup(db, 1) == 0, "pdbLookup 0 -> 0");
  CHECK(pdbLookup(db, 2) == 3, "pdbLookup 3 -> 3");
  CHECK(pdbLookup(db, 3) == 0, "pdbLookup sentinel -> 0");
  CHECK(pdbLookup(db, 4) == 7, "pdbLookup 7 -> 7");
  CHECK(pdbLookup(db, 99) == 0, "pdbLookup out-of-range -> 0");
  std::vector<std::uint8_t> empty;
  CHECK(pdbLookup(empty, 0) == 0, "pdbLookup empty -> 0");
}

void testBfsDb() {
  // Tiny line graph: 0-1-2-3-4, start at 0. Distances 0,1,2,3,4.
  constexpr std::size_t kN = 5;
  auto dist = bfsDb(kN, 0, [](std::uint32_t n) {
    std::vector<std::uint32_t> next;
    if (n > 0) next.push_back(n - 1);
    if (n + 1 < kN) next.push_back(n + 1);
    return next;
  });
  CHECK(dist.size() == kN, "bfsDb size matches graph");
  CHECK(dist[0] == 0, "bfsDb start distance 0");
  CHECK(dist[1] == 1, "bfsDb dist(1)==1");
  CHECK(dist[2] == 2, "bfsDb dist(2)==2");
  CHECK(dist[3] == 3, "bfsDb dist(3)==3");
  CHECK(dist[4] == 4, "bfsDb dist(4)==4");

  // Start in the middle: distances 2,1,0,1,2.
  auto distMid = bfsDb(kN, 2, [](std::uint32_t n) {
    std::vector<std::uint32_t> next;
    if (n > 0) next.push_back(n - 1);
    if (n + 1 < kN) next.push_back(n + 1);
    return next;
  });
  CHECK(distMid[0] == 2 && distMid[4] == 2 && distMid[2] == 0,
        "bfsDb from middle");

  // Isolated start: others stay at sentinel.
  auto distIso = bfsDb(kN, 0, [](std::uint32_t) {
    return std::vector<std::uint32_t>{};
  });
  CHECK(distIso[0] == 0 && distIso[1] == 255, "bfsDb unreachable sentinel");

  // Start out of range returns all-sentinel table.
  auto distBad = bfsDb(kN, 99, [](std::uint32_t) {
    return std::vector<std::uint32_t>{};
  });
  CHECK(distBad.size() == kN && distBad[0] == 255,
        "bfsDb bad start sentinel");
}

void testFindsSolution() {
  std::vector<int> allowed{0, 1};
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      searcher(&allowed, applyLine, isGoal(3), heuristicTo(3));
  auto res = searcher.run(0, 20);
  CHECK(res.has_value(), "searcher finds solution 0->3");
  if (res.has_value()) {
    CHECK(res->size() == 3, "solution length is 3");
    auto trace = replay(0, *res);
    CHECK(trace.back() == 3, "replayed path reaches goal");
    for (int m : *res) CHECK(m == 0, "only forward moves needed");
    CHECK(searcher.nodes() > 0, "node count tracked");
  }
  // Already at goal: empty path succeeds.
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      atGoal(&allowed, applyLine, isGoal(3), heuristicTo(3));
  auto res2 = atGoal.run(3, 20);
  CHECK(res2.has_value() && res2->empty(), "start at goal -> empty path");
}

void testRespectsBound() {
  std::vector<int> allowed{0, 1};
  // Goal 3 needs 3 moves; cap depth at 1 -> Unsolvable.
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      shallow(&allowed, applyLine, isGoal(3), heuristicTo(3));
  auto res = shallow.run(0, 1);
  CHECK(!res.has_value() && res.error() == SolveError::Unsolvable,
        "depth cap 1 -> Unsolvable");
  // Same start/goal with enough depth succeeds (bound raises correctly).
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      deep(&allowed, applyLine, isGoal(3), heuristicTo(3));
  auto res2 = deep.run(0, 3);
  CHECK(res2.has_value() && res2->size() == 3, "depth cap 3 -> solved");
  // Zero-content allowed list away from goal -> Unsolvable.
  std::vector<int> noMoves;
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      stuck(&noMoves, applyLine, isGoal(3), heuristicTo(3));
  auto res3 = stuck.run(0, 4);
  CHECK(!res3.has_value() && res3.error() == SolveError::Unsolvable,
        "no moves -> Unsolvable");
}

void testTimeout() {
  std::vector<int> allowed{0, 1};
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      searcher(&allowed, applyLine, isGoal(8), heuristicTo(8));
  // Deadline already expired -> Timeout.
  searcher.setDeadline(std::chrono::steady_clock::now() -
                       std::chrono::seconds(1));
  auto res = searcher.run(0, 20);
  CHECK(!res.has_value() && res.error() == SolveError::Timeout,
        "expired deadline -> Timeout");
}

void testCancelled() {
  std::vector<int> allowed{0, 1};
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      searcher(&allowed, applyLine, isGoal(8), heuristicTo(8));
  std::stop_source source;
  source.request_stop();
  searcher.setStopToken(source.get_token());
  auto res = searcher.run(0, 20);
  CHECK(!res.has_value() && res.error() == SolveError::Cancelled,
        "stopped token -> Cancelled");
}

}  // namespace

int main() {
  testPdbLookup();
  testBfsDb();
  testFindsSolution();
  testRespectsBound();
  testTimeout();
  testCancelled();
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}
