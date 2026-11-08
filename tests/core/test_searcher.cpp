#include <algorithm>
#include <array>
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

void testThreadEquivalence() {
  // Same toy search at thread_count 1 vs 2 must return identical
  // move sequences and visit the same node count (identical DFS order).
  std::vector<int> allowed{0, 1};
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      s1(&allowed, applyLine, isGoal(3), heuristicTo(3));
  s1.setThreadCount(1);
  auto r1 = s1.run(0, 20);

  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      s2(&allowed, applyLine, isGoal(3), heuristicTo(3));
  s2.setThreadCount(2);
  auto r2 = s2.run(0, 20);

  CHECK(r1.has_value() && r2.has_value(), "threads: both solve");
  if (r1.has_value() && r2.has_value()) {
    CHECK(*r1 == *r2, "threads: identical solution for tc 1 vs 2");
    CHECK(s1.nodes() == s2.nodes(), "threads: identical node count");
  }

  // Threaded cancelled search still honours the token.
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      s3(&allowed, applyLine, isGoal(8), heuristicTo(8));
  s3.setThreadCount(2);
  std::stop_source source;
  source.request_stop();
  s3.setStopToken(source.get_token());
  auto r3 = s3.run(0, 20);
  CHECK(!r3.has_value() && r3.error() == SolveError::Cancelled,
        "threads: stopped token still -> Cancelled");

  // Threaded expired deadline still honours the deadline.
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(heuristicTo(0))>
      s4(&allowed, applyLine, isGoal(8), heuristicTo(8));
  s4.setThreadCount(4);
  s4.setDeadline(std::chrono::steady_clock::now() -
                 std::chrono::seconds(1));
  auto r4 = s4.run(0, 20);
  CHECK(!r4.has_value() && r4.error() == SolveError::Timeout,
        "threads: expired deadline still -> Timeout");
}

void testStableOrderingWithThreads() {
  // Constant heuristic (all candidates tie) must preserve allowed-list
  // order identically at thread_count 1 and 2 (stable_sort behaviour).
  std::vector<int> allowed{1, 0};  // reversed order on purpose
  auto zero = [](int) { return 0; };
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(zero)>
      s1(&allowed, applyLine, isGoal(2), zero);
  s1.setThreadCount(1);
  auto r1 = s1.run(0, 20);

  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(zero)>
      s2(&allowed, applyLine, isGoal(2), zero);
  s2.setThreadCount(2);
  auto r2 = s2.run(0, 20);

  CHECK(r1.has_value() && r2.has_value(), "tie test: both solve");
  if (r1.has_value() && r2.has_value()) {
    CHECK(*r1 == *r2, "tie test: identical path tc1 vs tc2");
    // Only forward moves (0) can reach the goal; both must use them.
    for (int m : *r2) CHECK(m == 0, "tie test: forward moves used");
  }
}

void testHeapFallbackOver32() {
  // Move set wider than the 32-slot stack buffer must preserve semantics
  // via the heap fallback (and threaded root eval remains sequential).
  std::vector<int> allowed;
  for (int i = 0; i < 40; ++i) allowed.push_back(0);  // all forward
  auto zero = [](int) { return 0; };
  IdaSearcher<int, decltype(applyLine), decltype(isGoal(0)),
              decltype(zero)>
      s(&allowed, applyLine, isGoal(2), zero);
  auto r = s.run(0, 20);
  CHECK(r.has_value(), "fallback: wide move set still solves");
  if (r.has_value()) {
    CHECK(r->size() == 2, "fallback: shortest path length 2");
  }
}

// -- Baseline parity: shared DFS must match hand-rolled DFS order ----------
// Toy with same-face pruning via SearchState wrapper (as in cfop/roux/zz/
// heise): moves 0..5 with faces 0,0,1,1,2,2; same-face continuations are
// dead. Baseline replica implements the original hand-rolled dfs exactly:
// goal-first, heuristic/bound check, parent-side exclusion of over-bound
// children (bestExceed recorded at parent), std::sort by (f,h). Shared
// searcher must return the identical solution and visit the identical
// number of non-goal nodes. Before the fix (recursing into dead and
// over-bound children) this toy visited ~227k nodes vs ~38k baseline.
namespace {
struct ParityTS {
  int pos = 0;
  int prevFace = -1;
  bool dead = false;
};
constexpr int kParityMaxPos = 30;
constexpr int kParityGoal = 30;
constexpr int kParityMoves = 6;
constexpr int kParityFace[kParityMoves] = {0, 0, 1, 1, 2, 2};
constexpr int kParityDelta[kParityMoves] = {3, 2, 2, 1, 1, 0};
constexpr int kParityPruneH = 1 << 25;

int parityHeurNode(int pos) {
  int d = kParityGoal - pos;
  if (d < 0) d = 0;
  return d / 4;
}
ParityTS parityApply(const ParityTS& s, int move) {
  const int face = kParityFace[move];
  if (face == s.prevFace) return ParityTS{s.pos, s.prevFace, true};
  int p = s.pos + kParityDelta[move];
  if (p < 0) p = 0;
  if (p > kParityMaxPos) p = kParityMaxPos;
  return ParityTS{p, face, false};
}
bool parityGoal(const ParityTS& s) {
  return !s.dead && s.pos == kParityGoal;
}
int parityHeur(const ParityTS& s) {
  if (s.dead) return kParityPruneH;
  return parityHeurNode(s.pos);
}

// Hand-rolled replica: mirrors solver dfs(cur, g, prevFace, bound).
struct BaselineParity {
  std::vector<int> path;
  std::vector<int> solution;
  bool found = false;
  int bestExceed = 0;
  std::uint64_t nodes = 0;  // non-goal dfs calls (goal returns pre-increment)
  bool dfs(int pos, int prevFace, int g, int bound) {
    if (found) return true;
    if (pos == kParityGoal) {
      solution = path;
      found = true;
      return true;
    }
    ++nodes;
    const int h = parityHeurNode(pos);
    const int f = g + h;
    if (f > bound) {
      if (f < bestExceed) bestExceed = f;
      return false;
    }
    struct Cand {
      int h, f, mv, next;
    };
    std::array<Cand, 16> cands{};
    int n = 0;
    for (int mv = 0; mv < kParityMoves; ++mv) {
      if (kParityFace[mv] == prevFace) continue;
      int next = pos + kParityDelta[mv];
      if (next < 0) next = 0;
      if (next > kParityMaxPos) next = kParityMaxPos;
      const int hh = parityHeurNode(next);
      const int ff = g + 1 + hh;
      if (ff > bound) {
        if (ff < bestExceed) bestExceed = ff;
        continue;
      }
      cands[static_cast<std::size_t>(n)] = Cand{hh, ff, mv, next};
      ++n;
    }
    std::sort(cands.begin(), cands.begin() + n,
              [](const Cand& a, const Cand& b) {
                if (a.f != b.f) return a.f < b.f;
                return a.h < b.h;
              });
    for (int i = 0; i < n; ++i) {
      const auto& c = cands[static_cast<std::size_t>(i)];
      path.push_back(c.mv);
      if (dfs(c.next, kParityFace[c.mv], g + 1, bound)) return true;
      path.pop_back();
    }
    return false;
  }
  bool run(int start, int maxDepth) {
    if (start == kParityGoal) {
      solution.clear();
      found = true;
      return true;
    }
    int bound = parityHeurNode(start);
    while (true) {
      if (bound > maxDepth) return false;
      bestExceed = 2147483647;
      path.clear();
      found = false;
      (void)dfs(start, -1, 0, bound);
      if (found) return true;
      if (bestExceed == 2147483647) return false;
      bound = bestExceed;
    }
  }
};
}  // namespace

void testBaselineParity() {
  constexpr int kStart = 0;
  std::vector<int> allowed;
  for (int i = 0; i < kParityMoves; ++i) allowed.push_back(i);

  IdaSearcher<ParityTS, decltype(&parityApply), decltype(&parityGoal),
              decltype(&parityHeur)>
      shared(&allowed, &parityApply, &parityGoal, &parityHeur);
  auto rs = shared.run(ParityTS{kStart, -1, false}, 30);

  BaselineParity base;
  const bool ok = base.run(kStart, 30);

  CHECK(rs.has_value() && ok, "parity: both solve");
  if (rs.has_value() && ok) {
    CHECK(*rs == base.solution, "parity: identical solution path");
    std::cout << "parity nodes shared=" << shared.nodes()
              << " baseline=" << base.nodes << "\n";
    CHECK(shared.nodes() == base.nodes,
          "parity: identical non-goal node count");
    // Regression guard: pre-fix shared visited ~227k nodes on this toy.
    CHECK(shared.nodes() < 50000, "parity: no dead/over-bound recursion");
  }
}

}  // namespace

int main() {
  testPdbLookup();
  testBfsDb();
  testFindsSolution();
  testRespectsBound();
  testTimeout();
  testCancelled();
  testThreadEquivalence();
  testStableOrderingWithThreads();
  testHeapFallbackOver32();
  testBaselineParity();
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}
