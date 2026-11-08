// Tests for rubik::cfop (Cross -> F2L -> OLL -> PLL). See docs/SPEC.md
// Sections 7 and 10.
//
// Coverage:
//  - stage order and restricted move sets,
//  - stage predicates after each stage,
//  - random scrambles of depth 25 solved and verified via the independent
//    reference simulator (tests/core/reference_sim.hpp),
//  - timeout / cancelled / unsolvable / max-depth behaviour.
#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <random>
#include <stop_token>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "solvers/cfop/solver.hpp"
#include "../core/reference_sim.hpp"

namespace {

using rubik::CubeState;
using rubik::Move;
using rubik::SolveError;
using rubik::SolveOptions;
using rubik::Solution;

CubeState scrambleState(const std::vector<std::string>& tokens) {
  std::vector<Move> moves;
  for (const std::string& t : tokens) moves.push_back(Move{t});
  return rubik::apply(CubeState::solved(), moves);
}

CubeState scrambleStateText(const std::string& text) {
  auto parsed = rubik::parse_moves(text);
  if (!parsed) return CubeState::solved();
  return rubik::apply(CubeState::solved(), *parsed);
}

std::vector<std::string> solutionTokens(const Solution& sol) {
  std::vector<std::string> out;
  for (const auto& stage : sol.stages) {
    for (const std::string& t : stage.moves) out.push_back(t);
  }
  return out;
}

std::vector<std::string> allTokens() {
  std::vector<std::string> out;
  for (const Move& m : rubik::all_face_moves()) out.push_back(m.token);
  return out;
}

char faceOf(const std::string& tok) { return tok.empty() ? '?' : tok[0]; }

std::vector<std::string> randomScrambleTokens(std::mt19937& rng, int depth) {
  const std::vector<std::string> pool = allTokens();
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(depth));
  for (int i = 0; i < depth; ++i) {
    std::string pick;
    do {
      pick = pool[rng() % pool.size()];
    } while (!out.empty() && faceOf(pick) == faceOf(out.back()));
    out.push_back(pick);
  }
  return out;
}

SolveOptions defaultOptions() {
  SolveOptions opts;
  opts.timeout_ms = 60000;
  opts.max_depth = 20;
  opts.thread_count = 1;
  return opts;
}

bool contains(const std::vector<std::string>& v, const std::string& t) {
  for (const std::string& e : v) {
    if (e == t) return true;
  }
  return false;
}

std::string joinTokens(const std::vector<std::string>& tokens) {
  std::string out;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i != 0) out += ' ';
    out += tokens[i];
  }
  return out;
}

void expectStagePredicates(const CubeState& start, const Solution& sol) {
  CubeState cur = start;
  ASSERT_EQ(sol.stages.size(), 7u);
  // cross
  {
    std::vector<Move> ms;
    for (const std::string& t : sol.stages[0].moves) ms.push_back(Move{t});
    cur = rubik::apply(cur, ms);
    EXPECT_TRUE(rubik::cfop::is_cross_solved(cur))
        << "cross predicate after stage 0";
  }
  for (int k = 0; k < 4; ++k) {
    std::vector<Move> ms;
    for (const std::string& t : sol.stages[static_cast<std::size_t>(k + 1)].moves) {
      ms.push_back(Move{t});
    }
    cur = rubik::apply(cur, ms);
    EXPECT_TRUE(rubik::cfop::is_cross_solved(cur))
        << "cross preserved after f2l " << (k + 1);
    for (int j = 0; j <= k; ++j) {
      EXPECT_TRUE(rubik::cfop::is_f2l_pair_solved(cur, j))
          << "pair " << j << " after f2l " << (k + 1);
    }
  }
  {
    std::vector<Move> ms;
    for (const std::string& t : sol.stages[5].moves) ms.push_back(Move{t});
    cur = rubik::apply(cur, ms);
    EXPECT_TRUE(rubik::cfop::is_oll_solved(cur)) << "oll predicate";
    EXPECT_TRUE(rubik::cfop::is_f2l_solved(cur)) << "f2l preserved after oll";
  }
  {
    std::vector<Move> ms;
    for (const std::string& t : sol.stages[6].moves) ms.push_back(Move{t});
    cur = rubik::apply(cur, ms);
    EXPECT_TRUE(rubik::cfop::is_pll_solved(cur)) << "pll predicate";
    EXPECT_TRUE(rubik::is_solved(cur)) << "final state solved";
  }
}

void expectMoveSets(const Solution& sol) {
  const std::vector<std::string> expected = rubik::cfop::stage_names();
  ASSERT_EQ(sol.stages.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(sol.stages[i].stage_name, expected[i])
        << "stage order mismatch at index " << i;
    const std::vector<std::string> allowed =
        rubik::cfop::allowed_moves_for_stage(expected[i]);
    for (const std::string& tok : sol.stages[i].moves) {
      EXPECT_TRUE(contains(allowed, tok))
          << "token " << tok << " not allowed in stage " << expected[i];
      EXPECT_TRUE(rubik::is_valid_move_token(tok)) << "invalid token " << tok;
    }
  }
  // OLL must not contain D moves (PLL allows D).
  for (const std::string& tok : sol.stages[5].moves) {
    EXPECT_TRUE(tok.empty() || tok[0] != 'D') << "OLL uses D: " << tok;
  }
}

}  // namespace

TEST(Cfop, StageNamesAndMoveSets) {
  const std::vector<std::string> names = rubik::cfop::stage_names();
  const std::vector<std::string> expected = {"cross", "f2l_1", "f2l_2",
                                             "f2l_3", "f2l_4", "oll", "pll"};
  EXPECT_EQ(names, expected);
  EXPECT_EQ(rubik::cfop::cross_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::cfop::f2l_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::cfop::oll_allowed_moves().size(), 15u);
  EXPECT_EQ(rubik::cfop::pll_allowed_moves().size(), 18u);
  for (const std::string& tok : rubik::cfop::oll_allowed_moves()) {
    EXPECT_TRUE(tok.empty() || tok[0] != 'D') << "OLL allowed has D: " << tok;
  }
  bool crossHasD = false;
  for (const std::string& tok : rubik::cfop::cross_allowed_moves()) {
    if (!tok.empty() && tok[0] == 'D') crossHasD = true;
  }
  EXPECT_TRUE(crossHasD);
  EXPECT_EQ(rubik::cfop::allowed_moves_for_stage("cross").size(), 18u);
  EXPECT_EQ(rubik::cfop::allowed_moves_for_stage("oll").size(), 15u);
  EXPECT_EQ(rubik::cfop::allowed_moves_for_stage("pll").size(), 18u);
  EXPECT_EQ(rubik::cfop::allowed_moves_for_stage("unknown").size(), 0u);
}

TEST(Cfop, PredicatesOnSolved) {
  const CubeState solved = CubeState::solved();
  EXPECT_TRUE(rubik::cfop::is_cross_solved(solved));
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(rubik::cfop::is_f2l_pair_solved(solved, i));
  }
  EXPECT_TRUE(rubik::cfop::is_f2l_solved(solved));
  EXPECT_TRUE(rubik::cfop::is_oll_solved(solved));
  EXPECT_TRUE(rubik::cfop::is_pll_solved(solved));
  const CubeState r = scrambleStateText("R");
  EXPECT_FALSE(rubik::cfop::is_cross_solved(r));
  EXPECT_FALSE(rubik::cfop::is_f2l_solved(r));
  EXPECT_FALSE(rubik::cfop::is_pll_solved(r));
}

TEST(Cfop, SolvedCube) {
  auto res = rubik::cfop::solve(CubeState::solved(), defaultOptions());
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->total_moves(), 0);
  expectMoveSets(*res);
  expectStagePredicates(CubeState::solved(), *res);
}

TEST(Cfop, ShallowScrambles) {
  const std::vector<std::string> scrambles = {
      "R",
      "R U",
      "R U R' U'",
      "F R U R' U' F'",
      "R U R' U R U2 R'",
  };
  for (const std::string& text : scrambles) {
    const CubeState state = scrambleStateText(text);
    auto res = rubik::cfop::solve(state, defaultOptions());
    ASSERT_TRUE(res.has_value())
        << "scramble: " << text
        << " error: " << static_cast<int>(res.error());
    expectMoveSets(*res);
    expectStagePredicates(state, *res);
    // Reference-simulator verification: scramble + solution == solved.
    auto parsed = rubik::parse_moves(text);
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> scrambleTokens;
    for (const Move& m : *parsed) scrambleTokens.push_back(m.token);
    std::string f = refsim::apply_sequence(refsim::solved_facelets(),
                                           scrambleTokens);
    f = refsim::apply_sequence(f, solutionTokens(*res));
    EXPECT_EQ(f, refsim::solved_facelets()) << "scramble: " << text;
    // Core state also agrees with the reference simulator on the scramble.
    EXPECT_EQ(rubik::to_facelets(state),
              refsim::apply_sequence(refsim::solved_facelets(), scrambleTokens))
        << "core vs refsim on scramble: " << text;
  }
}

TEST(Cfop, RandomDepth25) {
  std::mt19937 rng(20261108u);
  const int kTrials = 5;
  for (int trial = 0; trial < kTrials; ++trial) {
    const std::vector<std::string> scramble = randomScrambleTokens(rng, 25);
    const CubeState state = scrambleState(scramble);
    // Cross-check core vs reference simulator on the scramble itself.
    EXPECT_EQ(rubik::to_facelets(state),
              refsim::apply_sequence(refsim::solved_facelets(), scramble))
        << "core vs refsim, trial " << trial;
    auto res = rubik::cfop::solve(state, defaultOptions());
    ASSERT_TRUE(res.has_value())
        << "trial " << trial << " scramble: " << joinTokens(scramble)
        << " error: " << (res.has_value() ? "none" : std::to_string(static_cast<int>(res.error())));
    expectMoveSets(*res);
    expectStagePredicates(state, *res);
    // Full verification via reference simulator.
    std::string f = refsim::apply_sequence(refsim::solved_facelets(), scramble);
    f = refsim::apply_sequence(f, solutionTokens(*res));
    EXPECT_EQ(f, refsim::solved_facelets())
        << "trial " << trial << " scramble: " << joinTokens(scramble)
        << " solution: " << joinTokens(solutionTokens(*res));
    std::cout << std::format("cfop trial {}: {} moves\n", trial,
                             res->total_moves());
  }
}

TEST(Cfop, TimeoutBehavior) {
  const CubeState state = scrambleStateText("R U F R' U' F' R U2 R' L' U L");
  SolveOptions opts;
  opts.timeout_ms = 1;
  opts.max_depth = 20;
  opts.thread_count = 1;
  auto res = rubik::cfop::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Cfop, CancelledBehavior) {
  const CubeState state = scrambleStateText("R U");
  std::stop_source src;
  src.request_stop();
  SolveOptions opts;
  opts.stop_token = src.get_token();
  opts.timeout_ms = 60000;
  opts.max_depth = 20;
  opts.thread_count = 1;
  auto res = rubik::cfop::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Cancelled);
}

TEST(Cfop, UnsolvableInvalidParity) {
  CubeState bad = CubeState::solved();
  std::swap(bad.cp[0], bad.cp[1]);
  auto res = rubik::cfop::solve(bad, defaultOptions());
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Unsolvable);
}

TEST(Cfop, MaxDepthRespected) {
  const CubeState state = scrambleStateText("R U F");
  SolveOptions opts;
  opts.timeout_ms = 60000;
  opts.max_depth = 0;
  opts.thread_count = 1;
  auto res = rubik::cfop::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Cfop, PllTablePreservesF2LAndOrientation) {
  const std::vector<std::string> table = rubik::cfop::pll_algorithms();
  EXPECT_EQ(table.size(), 21u);
  const std::vector<std::string> allowed = rubik::cfop::pll_allowed_moves();
  for (const std::string& alg : table) {
    auto parsed = rubik::parse_moves(alg);
    ASSERT_TRUE(parsed.has_value()) << "PLL alg parse failed: " << alg;
    for (const Move& m : *parsed) {
      EXPECT_TRUE(contains(allowed, m.token))
          << "PLL alg token not allowed: " << m.token << " in " << alg;
      EXPECT_TRUE(rubik::is_valid_move_token(m.token));
    }
    const CubeState after = rubik::apply(CubeState::solved(), *parsed);
    EXPECT_TRUE(rubik::cfop::is_oll_solved(after))
        << "PLL alg must preserve F2L+orientation: " << alg;
    EXPECT_FALSE(rubik::is_solved(after)) << "PLL alg is identity: " << alg;
  }
}

TEST(Cfop, PllTableSolvesAllPllStates) {
  // Enumerate all PLL states (F2L solved, oriented, U corners+edges
  // permuted with even total parity) and verify the table with U
  // pre/post rotations solves each via the core simulator.
  const std::vector<std::string> table = rubik::cfop::pll_algorithms();
  std::vector<std::vector<Move>> algs;
  for (const std::string& alg : table) {
    auto parsed = rubik::parse_moves(alg);
    ASSERT_TRUE(parsed.has_value()) << alg;
    algs.push_back(*parsed);
  }
  auto permParity = [](const std::uint8_t* p, int n) {
    int inv = 0;
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        if (p[i] > p[j]) ++inv;
      }
    }
    return inv % 2;
  };
  std::array<std::uint8_t, 4> cp0 = {0, 1, 2, 3};
  std::array<std::uint8_t, 4> ep0 = {0, 1, 2, 3};
  int tested = 0;
  int solvedCount = 0;
  do {
    do {
      CubeState s = CubeState::solved();
      for (int i = 0; i < 4; ++i) {
        s.cp[static_cast<std::size_t>(i)] = cp0[static_cast<std::size_t>(i)];
        s.ep[static_cast<std::size_t>(i)] = ep0[static_cast<std::size_t>(i)];
      }
      if (permParity(s.cp.data(), 8) != permParity(s.ep.data(), 12)) continue;
      if (!rubik::cfop::is_oll_solved(s)) continue;
      ++tested;
      bool solved = rubik::is_solved(s);
      if (!solved) {
        const std::vector<std::vector<std::string>> rots = {
            {}, {"U"}, {"U'"}, {"U2"}};
        for (const auto& only : rots) {
          std::vector<Move> seq;
          for (const std::string& t : only) seq.push_back(Move{t});
          if (rubik::is_solved(rubik::apply(s, seq))) {
            solved = true;
            break;
          }
        }
      }
      if (!solved) {
        for (const auto& alg : algs) {
          const std::vector<std::vector<std::string>> rots = {
              {}, {"U"}, {"U'"}, {"U2"}};
          for (const auto& pre : rots) {
            for (const auto& post : rots) {
              std::vector<Move> seq;
              for (const std::string& t : pre) seq.push_back(Move{t});
              for (const Move& m : alg) seq.push_back(m);
              for (const std::string& t : post) seq.push_back(Move{t});
              if (rubik::is_solved(rubik::apply(s, seq))) {
                solved = true;
                break;
              }
            }
            if (solved) break;
          }
          if (solved) break;
        }
      }
      if (solved) ++solvedCount;
      EXPECT_TRUE(solved)
          << "PLL state not solved by table: cp="
          << static_cast<int>(s.cp[0]) << static_cast<int>(s.cp[1])
          << static_cast<int>(s.cp[2]) << static_cast<int>(s.cp[3])
          << " ep=" << static_cast<int>(s.ep[0]) << static_cast<int>(s.ep[1])
          << static_cast<int>(s.ep[2]) << static_cast<int>(s.ep[3]);
    } while (std::next_permutation(ep0.begin(), ep0.end()));
  } while (std::next_permutation(cp0.begin(), cp0.end()));
  EXPECT_EQ(solvedCount, tested);
  EXPECT_GT(tested, 200);
}
