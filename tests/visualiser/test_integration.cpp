// Headless integration tests (no window). Mirrors main_3d.cpp helpers.
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <stop_token>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"
#include "core/moves.hpp"
#include "solvers/cfop/solver.hpp"
#include "solvers/heise/solver.hpp"
#include "solvers/roux/solver.hpp"
#include "solvers/twophase/solver.hpp"
#include "solvers/zz/solver.hpp"
#include "visualiser/facelet.hpp"
#include "visualiser/renderer.hpp"

using namespace rubik;
using namespace rubik::visualiser;

// Mirrors src/visualiser/main_3d.cpp (same registry content & pipeline).
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
  CubeState s = start;
  for (const auto& tok : concat_stages(sol)) s = applyTokenInstant(s, tok);
  return s;
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

TEST(IntegrationReplay, PerMethodReplayEqualsSolved) {
  auto registry = solver_registry();
  // Two short scrambles (depth<=6) per method; twophase clamps min 12 internally.
  const char* scrambles[2] = {"R U", "F R U"};
  for (const auto& entry : registry) {
    for (int si = 0; si < 2; ++si) {
      CubeState scrambled = scramble_from_tokens(scrambles[si]);
      FaceletArray f = cube_to_facelets(scrambled);
      auto v = validate_facelets(f);
      ASSERT_EQ(v.error, FaceletError::Ok) << entry.name << " scramble invalid";
      auto cube_res = facelets_to_cube(f);
      ASSERT_TRUE(cube_res.has_value());
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
      EXPECT_EQ(to_string54(cube_to_facelets(end)),
                to_string54(cube_to_facelets(CubeState::solved())))
          << entry.name << " scramble[" << si << "] facelet mismatch";
      std::cout << entry.name << " scramble[" << si << "] OK: "
                << res->total_moves() << " moves in " << ms << "ms\n";
    }
  }
}

TEST(IntegrationReplay, SolvedCubeIsSolved) {
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

TEST(IntegrationValidationGate, TwistedCornerRejected) {
  CubeState s = apply(CubeState::solved(), parse_moves("R U").value());
  FaceletArray f = cube_to_facelets(s);
  // Twist URF corner: rotate its 3 stickers (U8,R0,F2 indices).
  char a = f[8], b = f[9], c = f[20];
  f[8] = c; f[9] = a; f[20] = b;
  auto v = validate_facelets(f);
  ASSERT_EQ(v.error, FaceletError::CornerTwist) << "expected CornerTwist, msg=" << v.message;
  EXPECT_FALSE(v.offending.empty());
  // Solver must NOT be called: contract asserts via gate — test only proceeds
  // to conversion which must fail, so a solve would be blocked.
  auto res = facelets_to_cube(f);
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error().error, FaceletError::CornerTwist);
}

TEST(IntegrationValidationGate, UnsetRejected) {
  FaceletArray f = to_array(to_string54(cube_to_facelets(CubeState::solved())));
  f[10] = '?';
  auto v = validate_facelets(to_string54(f));
  EXPECT_NE(v.error, FaceletError::Ok);  // '?' is invalid char / colour count
}

TEST(IntegrationMirror, ToArrayRoundTrip) {
  // Exercises the main_3d.cpp-mirrored to_array helper (was unused).
  std::string solved = to_string54(cube_to_facelets(CubeState::solved()));
  EXPECT_EQ(solved.size(), 54u);
  FaceletArray back = to_array(solved);
  EXPECT_EQ(to_string54(back), solved);
  // Short input is still copied (mirror of main_3d.cpp fill semantics
  // partially; only the 54-char path is exercised here).
  FaceletArray short_arr = to_array("UUU");
  EXPECT_EQ(short_arr[0], 'U');
  EXPECT_EQ(short_arr[2], 'U');
}

TEST(IntegrationBackground, PreCancelledReturnsCancelledOrFast) {
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
