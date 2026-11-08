// Tests for rubik::optimal (IDA* + 4 PDBs). See docs/SPEC.md Sections 8,10.
//
// Coverage:
//  - successor/index correctness against the frozen core (independent of
//    solver tables: direct indices of rubik::apply successors),
//  - admissibility: PDB value <= true BFS distance. Checked two ways:
//    (a) pattern-space BFS to depth 6 for every PDB (exact pattern
//        distances; PDB must equal them), and (b) full-cube BFS to
//        depth 5 (heuristic <= full BFS layer). Full-cube depth 6 would
//        be ~9M states in a hash set, so depth 6 is covered exactly in
//        pattern space where the frontier is far smaller.
//  - optimal solutions for scrambles <=10 moves (valid, <= scramble),
//    and solver length == brute-force optimal for depth <=7,
//  - timeout / cancelled / DatabaseMissing (absent + mismatched files).
//
// PDB-dependent tests skip (not fail) when the DBs have not been built
// yet, so `ctest` stays green before `build_pattern_db` runs; the
// acceptance run builds the DBs first and exercises them fully.
// Build wiring for this file (root CMake) is owned by the main agent;
// compile manually against rubik_optimal + gtest until then.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "solvers/optimal/solver.hpp"

namespace {

using rubik::CubeState;
using rubik::Move;
using rubik::SolveError;
using rubik::SolveOptions;
using rubik::Solution;

CubeState scramble_state(std::string_view scramble) {
  auto parsed = rubik::parse_moves(scramble);
  if (!parsed) return CubeState::solved();
  return rubik::apply(CubeState::solved(), *parsed);
}

std::vector<Move> solution_moves(const Solution& sol) {
  std::vector<Move> out;
  for (const auto& stage : sol.stages) {
    for (const auto& tok : stage.moves) out.push_back(Move{tok});
  }
  return out;
}

bool pdb_files_present() {
  const std::filesystem::path dir = rubik::optimal::resolve_data_dir();
  std::error_code ec;
  return std::filesystem::exists(dir / rubik::optimal::kCornerFileName, ec) &&
         std::filesystem::exists(dir / rubik::optimal::kEdge1FileName, ec) &&
         std::filesystem::exists(dir / rubik::optimal::kEdge2FileName, ec) &&
         std::filesystem::exists(dir / rubik::optimal::kEdge3FileName, ec);
}

std::optional<std::array<std::vector<std::uint8_t>, 4>> load_dbs() {
  const std::filesystem::path dir = rubik::optimal::resolve_data_dir();
  auto c = rubik::optimal::load_pdb_file(
      dir / rubik::optimal::kCornerFileName, rubik::optimal::kPdbIdCorner,
      rubik::optimal::kCornerEntries);
  if (!c) return std::nullopt;
  auto e1 = rubik::optimal::load_pdb_file(
      dir / rubik::optimal::kEdge1FileName, rubik::optimal::kPdbIdEdge1,
      rubik::optimal::kEdgeEntries);
  if (!e1) return std::nullopt;
  auto e2 = rubik::optimal::load_pdb_file(
      dir / rubik::optimal::kEdge2FileName, rubik::optimal::kPdbIdEdge2,
      rubik::optimal::kEdgeEntries);
  if (!e2) return std::nullopt;
  auto e3 = rubik::optimal::load_pdb_file(
      dir / rubik::optimal::kEdge3FileName, rubik::optimal::kPdbIdEdge3,
      rubik::optimal::kEdgeEntries);
  if (!e3) return std::nullopt;
  std::array<std::vector<std::uint8_t>, 4> dbs;
  dbs[0] = std::move(*c);
  dbs[1] = std::move(*e1);
  dbs[2] = std::move(*e2);
  dbs[3] = std::move(*e3);
  return dbs;
}

int heuristic_of(const CubeState& s,
                 const std::array<std::vector<std::uint8_t>, 4>& dbs) {
  int h = rubik::optimal::pdb_get(dbs[0], rubik::optimal::corner_index(s));
  for (int p = 0; p < 3; ++p) {
    h = (std::max)(h, static_cast<int>(rubik::optimal::pdb_get(
                           dbs[static_cast<std::size_t>(p + 1)],
                           rubik::optimal::edge_index(s, p))));
  }
  return h;
}

// Independent cubie move table for full-cube BFS, derived from the frozen
// core exactly as the solver does but coded separately here so BFS does
// not reuse solver ranking/successor code.
struct TestMove {
  std::array<std::uint8_t, 8> cp{};
  std::array<std::uint8_t, 8> co{};
  std::array<std::uint8_t, 12> ep{};
  std::array<std::uint8_t, 12> eo{};
};

std::vector<TestMove> test_moves() {
  std::vector<TestMove> out;
  for (const Move& m : rubik::all_face_moves()) {
    const CubeState after = rubik::apply(CubeState::solved(), m);
    TestMove tm;
    tm.cp = after.cp;
    tm.co = after.co;
    tm.ep = after.ep;
    tm.eo = after.eo;
    out.push_back(tm);
  }
  return out;
}

CubeState fast_apply(const CubeState& s, const TestMove& m) {
  CubeState n = s;
  for (int p = 0; p < 8; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    n.cp[pp] = s.cp[m.cp[pp]];
    n.co[pp] = static_cast<std::uint8_t>((s.co[m.cp[pp]] + m.co[pp]) % 3);
  }
  for (int p = 0; p < 12; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    n.ep[pp] = s.ep[m.ep[pp]];
    n.eo[pp] = static_cast<std::uint8_t>(s.eo[m.ep[pp]] ^ m.eo[pp]);
  }
  return n;
}

struct CubeHash {
  std::size_t operator()(const CubeState& s) const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint8_t v) {
      h ^= v;
      h *= 1099511628211ull;
    };
    for (std::uint8_t v : s.cp) mix(v);
    for (std::uint8_t v : s.co) mix(v);
    for (std::uint8_t v : s.ep) mix(v);
    for (std::uint8_t v : s.eo) mix(v);
    return static_cast<std::size_t>(h);
  }
};

// Brute-force optimal length via depth-limited DFS with PDB pruning
// (admissibility already established by the BFS tests above). Returns
// the length, or -1 when no solution exists within max_depth.
int brute_optimal_length(
    const CubeState& start,
    const std::array<std::vector<std::uint8_t>, 4>& dbs, int max_depth) {
  const std::vector<TestMove> moves = test_moves();
  const CubeState solved_state = CubeState::solved();
  for (int limit = 0; limit <= max_depth; ++limit) {
    std::function<bool(const CubeState&, int, int)> dfs =
        [&](const CubeState& cur, int g, int prev_face) -> bool {
      if (cur == solved_state) return true;
      if (g == limit) return false;
      for (int m = 0; m < 18; ++m) {
        if ((m / 3) == prev_face) continue;
        const CubeState nxt =
            fast_apply(cur, moves[static_cast<std::size_t>(m)]);
        if (g + 1 + heuristic_of(nxt, dbs) > limit) continue;
        if (dfs(nxt, g + 1, m / 3)) return true;
      }
      return false;
    };
    if (dfs(start, 0, -1)) return limit;
  }
  return -1;
}

}  // namespace

TEST(Optimal, SuccessorMatchesCoreApply) {
  const std::vector<Move> moves = rubik::all_face_moves();
  std::mt19937 rng(20261108u);
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<Move> seq;
    const int len = static_cast<int>(rng() % 18u);
    for (int i = 0; i < len; ++i) {
      seq.push_back(moves[rng() % moves.size()]);
    }
    const CubeState base = rubik::apply(CubeState::solved(), seq);
    const std::uint32_t cidx = rubik::optimal::corner_index(base);
    std::uint32_t eidx[3] = {0u, 0u, 0u};
    for (int p = 0; p < 3; ++p) eidx[p] = rubik::optimal::edge_index(base, p);
    for (int m = 0; m < 18; ++m) {
      const CubeState ref =
          rubik::apply(base, moves[static_cast<std::size_t>(m)]);
      EXPECT_EQ(rubik::optimal::corner_successor(cidx, m),
                rubik::optimal::corner_index(ref))
          << "corner move " << moves[static_cast<std::size_t>(m)].token;
      for (int p = 0; p < 3; ++p) {
        EXPECT_EQ(rubik::optimal::edge_successor(eidx[p], p, m),
                  rubik::optimal::edge_index(ref, p))
            << "edge pdb " << p << " move "
            << moves[static_cast<std::size_t>(m)].token;
      }
    }
  }
  EXPECT_EQ(rubik::optimal::corner_successor(12345u, -1), 12345u);
  EXPECT_EQ(rubik::optimal::corner_successor(12345u, 18), 12345u);
  EXPECT_EQ(rubik::optimal::edge_successor(999u, 0, 99), 999u);
}

TEST(Optimal, IndicesSolvedZeroAndInRange) {
  // Corner solved is 0; edge solved indices are non-zero in general
  // (cubie-centric ranking) and must match solved_pattern_index().
  EXPECT_EQ(rubik::optimal::corner_index(CubeState::solved()), 0u);
  EXPECT_EQ(rubik::optimal::solved_pattern_index(0u), 0u);
  for (int p = 0; p < 3; ++p) {
    EXPECT_EQ(rubik::optimal::edge_index(CubeState::solved(), p),
              rubik::optimal::solved_pattern_index(
                  static_cast<std::uint32_t>(p + 1)));
  }
  const std::vector<Move> moves = rubik::all_face_moves();
  std::mt19937 rng(42u);
  for (int trial = 0; trial < 100; ++trial) {
    std::vector<Move> seq;
    for (int i = 0; i < 12; ++i) seq.push_back(moves[rng() % moves.size()]);
    const CubeState s = rubik::apply(CubeState::solved(), seq);
    EXPECT_LT(rubik::optimal::corner_index(s),
              rubik::optimal::kCornerEntries);
    for (int p = 0; p < 3; ++p) {
      EXPECT_LT(rubik::optimal::edge_index(s, p),
                rubik::optimal::kEdgeEntries);
    }
  }
}

TEST(Optimal, AdmissibilityPatternBfsDepth6) {
  auto dbs = load_dbs();
  if (!dbs) GTEST_SKIP() << "PDB files not built (run build_pattern_db)";
  // BFS each pattern space from its solved pattern to depth 6. First
  // discovery distance is the true pattern BFS distance, so the stored
  // PDB value must equal it (in particular <= it, i.e. admissible) for
  // every pattern state within depth 6.
  for (int pdb = 0; pdb < 4; ++pdb) {
    const std::uint32_t entries = (pdb == 0) ? rubik::optimal::kCornerEntries
                                             : rubik::optimal::kEdgeEntries;
    const std::uint32_t start = rubik::optimal::solved_pattern_index(
        static_cast<std::uint32_t>(pdb));
    std::vector<std::uint8_t> visited(entries, 0u);
    std::vector<std::uint32_t> frontier{start};
    visited[start] = 1u;
    EXPECT_EQ(rubik::optimal::pdb_get(
                  (*dbs)[static_cast<std::size_t>(pdb)], start),
              0);
    for (int depth = 0; depth < 6; ++depth) {
      std::vector<std::uint32_t> next;
      next.reserve(frontier.size() * 4u);
      for (std::uint32_t idx : frontier) {
        for (int m = 0; m < 18; ++m) {
          const std::uint32_t succ =
              (pdb == 0)
                  ? rubik::optimal::corner_successor(idx, m)
                  : rubik::optimal::edge_successor(idx, pdb - 1, m);
          if (visited[succ] == 0u) {
            visited[succ] = 1u;
            next.push_back(succ);
            EXPECT_EQ(rubik::optimal::pdb_get(
                          (*dbs)[static_cast<std::size_t>(pdb)], succ),
                      depth + 1)
                << "pdb " << pdb << " index " << succ;
          }
        }
      }
      frontier = std::move(next);
      if (frontier.empty()) break;
    }
  }
}

TEST(Optimal, AdmissibilityFullCubeDepth5) {
  auto dbs = load_dbs();
  if (!dbs) GTEST_SKIP() << "PDB files not built (run build_pattern_db)";
  const std::vector<TestMove> moves = test_moves();
  std::unordered_set<CubeState, CubeHash> visited;
  visited.reserve(800000u);
  std::vector<CubeState> frontier{CubeState::solved()};
  visited.insert(CubeState::solved());
  EXPECT_LE(heuristic_of(CubeState::solved(), *dbs), 0);
  for (int depth = 1; depth <= 5; ++depth) {
    std::vector<CubeState> next;
    next.reserve(frontier.size() * 8u);
    for (const CubeState& s : frontier) {
      for (const TestMove& tm : moves) {
        const CubeState ns = fast_apply(s, tm);
        if (visited.insert(ns).second) {
          next.push_back(ns);
          EXPECT_LE(heuristic_of(ns, *dbs), depth)
              << "heuristic exceeds true BFS distance at depth " << depth;
        }
      }
    }
    frontier = std::move(next);
    ASSERT_FALSE(frontier.empty());
  }
}

TEST(Optimal, SolvedCubeEmptySolution) {
  if (!pdb_files_present()) {
    GTEST_SKIP() << "PDB files not built (run build_pattern_db)";
  }
  SolveOptions opts;
  opts.timeout_ms = 30000;
  opts.max_depth = 20;
  opts.thread_count = 4;
  auto res = rubik::optimal::solve(CubeState::solved(), opts);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->total_moves(), 0);
}

TEST(Optimal, OptimalShallowScrambles) {
  if (!pdb_files_present()) {
    GTEST_SKIP() << "PDB files not built (run build_pattern_db)";
  }
  const std::vector<std::string> scrambles = {
      "R",
      "R U",
      "R U R'",
      "R U R' U'",
      "F R U R' U' F'",
      "R U R' U R U2 R'",
      "R U F R' U' F' R U2 R'",
      "R U R' L' U' L U R U2",
      "R U F R' U' F' R U2 R' L' U",
      "R U R' U R U2 R' L' U' L U L'",
  };
  SolveOptions opts;
  opts.timeout_ms = 30000;
  opts.max_depth = 20;
  opts.thread_count = 4;
  for (const std::string& scramble : scrambles) {
    const CubeState state = scramble_state(scramble);
    auto res = rubik::optimal::solve(state, opts);
    ASSERT_TRUE(res.has_value())
        << "scramble: " << scramble
        << " error: " << static_cast<int>(res.error());
    auto parsed = rubik::parse_moves(scramble);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_LE(res->total_moves(),
              static_cast<int>(parsed->size()))
        << "scramble: " << scramble;
    const CubeState end = rubik::apply(state, solution_moves(*res));
    EXPECT_TRUE(rubik::is_solved(end)) << "scramble: " << scramble;
  }
}

TEST(Optimal, OptimalMatchesBruteForceForDepthUpTo7) {
  auto dbs = load_dbs();
  if (!dbs) GTEST_SKIP() << "PDB files not built (run build_pattern_db)";
  const std::vector<std::string> scrambles = {
      "R U",
      "R U R'",
      "R U R' U'",
      "F R U R' U' F'",
      "R U R' U R U2 R'",
  };
  SolveOptions opts;
  opts.timeout_ms = 30000;
  opts.max_depth = 20;
  opts.thread_count = 2;
  for (const std::string& scramble : scrambles) {
    const CubeState state = scramble_state(scramble);
    auto res = rubik::optimal::solve(state, opts);
    ASSERT_TRUE(res.has_value()) << "scramble: " << scramble;
    const int brute = brute_optimal_length(state, *dbs, 7);
    ASSERT_GE(brute, 0) << "scramble: " << scramble;
    EXPECT_EQ(res->total_moves(), brute) << "scramble: " << scramble;
  }
}

TEST(Optimal, TimeoutBehavior) {
  if (!pdb_files_present()) {
    GTEST_SKIP() << "PDB files not built (run build_pattern_db)";
  }
  const CubeState state =
      scramble_state("R U F R' U' F' R U2 R' L' U L");
  SolveOptions opts;
  opts.timeout_ms = 1;
  opts.max_depth = 20;
  opts.thread_count = 4;
  auto res = rubik::optimal::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Optimal, CancelledBehavior) {
  const CubeState state = scramble_state("R U");
  std::stop_source src;
  src.request_stop();
  SolveOptions opts;
  opts.stop_token = src.get_token();
  opts.timeout_ms = 30000;
  opts.max_depth = 20;
  opts.thread_count = 4;
  auto res = rubik::optimal::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Cancelled);
}

TEST(Optimal, DatabaseMissingOnAbsentDb) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "rubik_optimal_missing_db";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  ASSERT_FALSE(ec);
  rubik::optimal::set_data_dir(dir);
  const CubeState state = scramble_state("R U");
  SolveOptions opts;
  auto res = rubik::optimal::solve(state, opts);
  rubik::optimal::set_data_dir({});
  std::filesystem::remove_all(dir, ec);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::DatabaseMissing);
}

TEST(Optimal, DatabaseMissingOnMismatchedDb) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "rubik_optimal_bad_db";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  ASSERT_FALSE(ec);
  {
    std::ofstream out(dir / rubik::optimal::kCornerFileName,
                      std::ios::binary | std::ios::trunc);
    out << "this is not a valid pdb file";
  }
  rubik::optimal::set_data_dir(dir);
  const CubeState state = scramble_state("R U");
  SolveOptions opts;
  auto res = rubik::optimal::solve(state, opts);
  rubik::optimal::set_data_dir({});
  std::filesystem::remove_all(dir, ec);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::DatabaseMissing);
}

TEST(Optimal, UnsolvableInvalidParity) {
  CubeState bad = CubeState::solved();
  // Swap two corners: odd corner parity vs even edge parity.
  std::swap(bad.cp[0], bad.cp[1]);
  SolveOptions opts;
  auto res = rubik::optimal::solve(bad, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Unsolvable);
}

TEST(Optimal, MaxDepthRespected) {
  if (!pdb_files_present()) {
    GTEST_SKIP() << "PDB files not built (run build_pattern_db)";
  }
  const CubeState state = scramble_state("R U R' U' F");
  SolveOptions opts;
  opts.timeout_ms = 30000;
  opts.max_depth = 2;
  opts.thread_count = 4;
  auto res = rubik::optimal::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}
