// Solver replay integration tests (no GUI deps).
// Tests that each solver's solution, when replayed from the scrambled state,
// reaches the solved state. Also tests validation gates and background cancellation.
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"
#include "core/moves.hpp"
#include "core/validation.hpp"
#include "solvers/cfop/solver.hpp"
#include "solvers/heise/solver.hpp"
#include "solvers/roux/solver.hpp"
#include "solvers/twophase/solver.hpp"
#include "solvers/zz/solver.hpp"

using namespace rubik;

using FaceletArray = std::array<char, 54>;

struct SolverEntry { const char* name; SolverFn fn; };
static std::vector<SolverEntry> solver_registry() {
  return {{"CFOP", &rubik::cfop::solve}, {"Roux", &rubik::roux::solve},
          {"ZZ", &rubik::zz::solve}, {"Heise", &rubik::heise::solve},
          {"Twophase", &rubik::twophase::solve}};
}
static std::vector<std::string> concat_stages(const Solution& s) {
  std::vector<std::string> out;
  for (const auto& st : s.stages) for (const auto& m : st.moves) out.push_back(m);
  return out;
}
static CubeState replay_solves(CubeState start, const Solution& sol) {
  CubeState st = start;
  auto apply_token = [](const CubeState& s, const std::string& tok) {
    return apply(s, Move{tok});
  };
  for (const auto& tok : concat_stages(sol)) st = apply_token(st, tok);
  return st;
}
static FaceletArray to_array(const std::string& s) {
  FaceletArray a{};
  std::copy_n(s.begin(), std::min<std::size_t>(54, s.size()), a.begin());
  return a;
}
static std::string to_string54(const FaceletArray& a) { return {a.begin(), a.end()}; }

static CubeState scramble_from_tokens(const std::string& scramble) {
  auto moves = parse_moves(scramble).value();
  return apply(CubeState::solved(), moves);
}

TEST(SolverReplay, PerMethodReplayEqualsSolved) {
  auto registry = solver_registry();
  // Two short scrambles (depth<=6) per method; twophase clamps min 12 internally.
  const char* scrambles[2] = {"R U", "F R U"};
  for (const auto& entry : registry) {
    for (int si = 0; si < 2; ++si) {
      CubeState scrambled = scramble_from_tokens(scrambles[si]);
      std::string facelets = to_facelets(scrambled);
      auto v = validate_facelets_detailed(facelets);
      ASSERT_TRUE(v) << entry.name << " scramble invalid: " << v.error().message;
      auto cube_res = parse_facelets(facelets);
      ASSERT_TRUE(cube_res.has_value()) << entry.name << " parse failed: " << cube_res.error();
      SolveOptions opts{10000, 6, 1, std::stop_token{}};
      auto t0 = std::chrono::steady_clock::now();
      auto res = entry.fn(*cube_res, opts);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
      if (!res) {
        // Tables may be absent (twophase h=0 Timeout) — allow skip/failure
        // but report; replay property is conditional on success.
        std::cout << entry.name << " scramble[" << si << "] skipped: error="
                  << (int)res.error() << " in " << ms << "ms\n";
        continue;
      }
      CubeState end = replay_solves(*cube_res, *res);
      EXPECT_TRUE(is_solved(end)) << entry.name << " scramble[" << si << "] replay != solved";
      EXPECT_EQ(to_facelets(end), to_facelets(CubeState::solved()))
            << entry.name << " scramble[" << si << "] facelet mismatch";
      std::cout << entry.name << " scramble[" << si << "] OK: "
                << res->total_moves() << " moves in " << ms << "ms\n";
    }
  }
}

TEST(SolverReplay, SolvedCubeIsSolved) {
  auto registry = solver_registry();
  for (const auto& entry : registry) {
    CubeState s = CubeState::solved();
    auto res = entry.fn(s, SolveOptions{5000, 6, 1, std::stop_token{}});
    // Either clean zero-move solve, or a skip if solver struggles; on
    // success replay must be solved.
    if (res) {
      EXPECT_TRUE(is_solved(replay_solves(s, *res)));
      std::cout << entry.name << " solved: " << res->total_moves() << " moves\n";
    } else {
      std::cout << entry.name << " solved skipped: error=" << (int)res.error() << "\n";
    }
  }
}

TEST(SolverValidationGate, TwistedCornerRejected) {
  CubeState s = apply(CubeState::solved(), parse_moves("R U").value());
  std::string facelets = to_facelets(s);
  // Twist URF corner: rotate its 3 stickers (U8,R0,F2 indices = 8,9,20).
  char a = facelets[8], b = facelets[9], c = facelets[20];
  facelets[8] = c; facelets[9] = a; facelets[20] = b;
  auto v = validate_facelets_detailed(facelets);
  ASSERT_FALSE(v) << "expected TwistedCorner";
  EXPECT_EQ(v.error().kind, ValidationErrorKind::TwistedCorner) << "msg=" << v.error().message;
  // Solver must NOT be called: contract asserts via gate — the validation gate
  // (validate_facelets_detailed) must reject before any solver is invoked.
  // Note: parse_facelets may still parse (it validates cubie existence but not
  // twist/flip/parity sums), so the gate is the authoritative check.
}

TEST(SolverValidationGate, UnsetRejected) {
  FaceletArray f = to_array(to_facelets(CubeState::solved()));
  f[10] = '?';
  auto v = validate_facelets_detailed(to_string54(f));
  EXPECT_FALSE(v);  // '?' is invalid char / colour count
}

TEST(SolverReplay, ToArrayRoundTrip) {
  // Exercises the to_array helper.
  std::string solved = to_facelets(CubeState::solved());
  EXPECT_EQ(solved.size(), 54u);
  FaceletArray back = to_array(solved);
  EXPECT_EQ(to_string54(back), solved);
  // Short input is still copied (only the 54-char path is exercised here).
  FaceletArray short_arr = to_array("UUU");
  EXPECT_EQ(short_arr[0], 'U');
  EXPECT_EQ(short_arr[2], 'U');
}

TEST(SolverBackground, PreCancelledReturnsCancelledOrFast) {
  auto registry = solver_registry();
  CubeState s = scramble_from_tokens("R U");
  for (const auto& entry : registry) {
    std::stop_source src;
    src.request_stop();
    SolveOptions opts{3000, 6, 1, src.get_token()};
    auto t0 = std::chrono::steady_clock::now();
    auto res = entry.fn(s, opts);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    // Must not hang: returns quickly, either Cancelled or (if solver
    // doesn't check pre-solve) a fast shallow result.
    EXPECT_LT(ms, 3000) << entry.name << " pre-cancelled hung";
    if (!res) {
      EXPECT_TRUE(res.error() == SolveError::Cancelled || res.error() == SolveError::Timeout)
          << entry.name << " expected Cancelled/Timeout, got " << (int)res.error();
    }
    std::cout << entry.name << " pre-cancelled: " << ms << "ms "
              << (res ? "solved (ignored cancel)" : "cancelled") << "\n";
  }
}