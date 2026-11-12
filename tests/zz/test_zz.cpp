// Tests for rubik::zz (EOLine -> ZZF2L left/right -> COLL -> EPLL).
// See docs/SPEC.md Sections 7 and 10.
//
// Coverage:
//  - stage order and restricted move sets (esp. ZZF2L/COLL/EPLL R/L/U only),
//  - stage predicates after each stage,
//  - R/L/U moves preserve EOLine (EO + DF/DB) step by step,
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
#include "solvers/zz/solver.hpp"
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
  opts.timeout_ms = 120000;
  opts.max_depth = 30;
  opts.thread_count = 1;
  return opts;
}

bool contains(const std::vector<std::string>& v, const std::string& t) {
  for (const std::string& e : v) {
    if (e == t) return true;
  }
  return false;
}

bool isZzToken(const std::string& t) {
  return t == "R" || t == "R'" || t == "R2" || t == "L" || t == "L'" ||
         t == "L2" || t == "U" || t == "U'" || t == "U2";
}

std::string joinTokens(const std::vector<std::string>& tokens) {
  std::string out;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i != 0) out += ' ';
    out += tokens[i];
  }
  return out;
}

CubeState applyTokens(const CubeState& start,
                      const std::vector<std::string>& tokens) {
  std::vector<Move> ms;
  for (const std::string& t : tokens) ms.push_back(Move{t});
  return rubik::apply(start, ms);
}

void expectStagePredicates(const CubeState& start, const Solution& sol) {
  CubeState cur = start;
  ASSERT_EQ(sol.stages.size(), 5u);
  cur = applyTokens(cur, sol.stages[0].moves);
  EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
      << "eoline predicate after stage 0";
  cur = applyTokens(cur, sol.stages[1].moves);
  EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
      << "eoline preserved after zzf2l_left";
  EXPECT_TRUE(rubik::zz::is_zzf2l_left_solved(cur))
      << "left block predicate after stage 1";
  cur = applyTokens(cur, sol.stages[2].moves);
  EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
      << "eoline preserved after zzf2l_right";
  EXPECT_TRUE(rubik::zz::is_zzf2l_right_solved(cur))
      << "right block predicate after stage 2";
  EXPECT_TRUE(rubik::zz::is_zzf2l_solved(cur)) << "zzf2l predicate";
  cur = applyTokens(cur, sol.stages[3].moves);
  EXPECT_TRUE(rubik::zz::is_coll_solved(cur)) << "coll predicate";
  EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
      << "eoline preserved after coll";
  cur = applyTokens(cur, sol.stages[4].moves);
  EXPECT_TRUE(rubik::zz::is_eppl_solved(cur)) << "eppl predicate";
  EXPECT_TRUE(rubik::is_solved(cur)) << "final state solved";
}

void expectMoveSets(const Solution& sol) {
  const std::vector<std::string> expected = rubik::zz::stage_names();
  ASSERT_EQ(sol.stages.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(sol.stages[i].stage_name, expected[i])
        << "stage order mismatch at index " << i;
    const std::vector<std::string> allowed =
        rubik::zz::allowed_moves_for_stage(expected[i]);
    for (const std::string& tok : sol.stages[i].moves) {
      EXPECT_TRUE(contains(allowed, tok))
          << "token " << tok << " not allowed in stage " << expected[i];
      EXPECT_TRUE(rubik::is_valid_move_token(tok)) << "invalid token " << tok;
    }
  }
  // ZZF2L / COLL / EPLL must use only R, L and U moves.
  for (std::size_t i = 1; i < sol.stages.size(); ++i) {
    for (const std::string& tok : sol.stages[i].moves) {
      EXPECT_TRUE(isZzToken(tok))
          << "stage " << sol.stages[i].stage_name
          << " uses non-R/L/U token: " << tok;
    }
  }
}

}  // namespace

TEST(Zz, StageNamesAndMoveSets) {
  const std::vector<std::string> names = rubik::zz::stage_names();
  const std::vector<std::string> expected = {"eoline", "zzf2l_left",
                                             "zzf2l_right", "coll", "eppl"};
  EXPECT_EQ(names, expected);
  EXPECT_EQ(rubik::zz::eoline_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::zz::zzf2l_allowed_moves().size(), 9u);
  EXPECT_EQ(rubik::zz::zzf2l_left_allowed_moves().size(), 9u);
  EXPECT_EQ(rubik::zz::zzf2l_right_allowed_moves().size(), 9u);
  EXPECT_EQ(rubik::zz::coll_allowed_moves().size(), 9u);
  EXPECT_EQ(rubik::zz::eppl_allowed_moves().size(), 9u);
  for (const std::string& tok : rubik::zz::zzf2l_allowed_moves()) {
    EXPECT_TRUE(isZzToken(tok)) << "ZZF2L allowed has non-R/L/U: " << tok;
  }
  for (const std::string& tok : rubik::zz::coll_allowed_moves()) {
    EXPECT_TRUE(isZzToken(tok)) << "COLL allowed has non-R/L/U: " << tok;
  }
  for (const std::string& tok : rubik::zz::eppl_allowed_moves()) {
    EXPECT_TRUE(isZzToken(tok)) << "EPLL allowed has non-R/L/U: " << tok;
  }
  EXPECT_TRUE(contains(rubik::zz::zzf2l_allowed_moves(), "R"));
  EXPECT_TRUE(contains(rubik::zz::zzf2l_allowed_moves(), "L"));
  EXPECT_TRUE(contains(rubik::zz::zzf2l_allowed_moves(), "U"));
  EXPECT_EQ(rubik::zz::allowed_moves_for_stage("eoline").size(), 18u);
  EXPECT_EQ(rubik::zz::allowed_moves_for_stage("zzf2l_left").size(), 9u);
  EXPECT_EQ(rubik::zz::allowed_moves_for_stage("zzf2l_right").size(), 9u);
  EXPECT_EQ(rubik::zz::allowed_moves_for_stage("coll").size(), 9u);
  EXPECT_EQ(rubik::zz::allowed_moves_for_stage("eppl").size(), 9u);
  EXPECT_EQ(rubik::zz::allowed_moves_for_stage("unknown").size(), 0u);
}

TEST(Zz, PredicatesOnSolved) {
  const CubeState solved = CubeState::solved();
  EXPECT_TRUE(rubik::zz::is_eoline_solved(solved));
  EXPECT_TRUE(rubik::zz::is_zzf2l_left_solved(solved));
  EXPECT_TRUE(rubik::zz::is_zzf2l_right_solved(solved));
  EXPECT_TRUE(rubik::zz::is_zzf2l_solved(solved));
  EXPECT_TRUE(rubik::zz::is_coll_solved(solved));
  EXPECT_TRUE(rubik::zz::is_eppl_solved(solved));
  // F flips edges, so EOLine breaks.
  const CubeState f = scrambleStateText("F");
  EXPECT_FALSE(rubik::zz::is_eoline_solved(f));
  EXPECT_FALSE(rubik::zz::is_zzf2l_solved(f));
  EXPECT_FALSE(rubik::zz::is_coll_solved(f));
  EXPECT_FALSE(rubik::zz::is_eppl_solved(f));
  // L breaks the left block; R breaks the right block.
  const CubeState l = scrambleStateText("L");
  EXPECT_FALSE(rubik::zz::is_zzf2l_left_solved(l));
  EXPECT_FALSE(rubik::zz::is_zzf2l_right_solved(l));
  const CubeState r = scrambleStateText("R");
  EXPECT_FALSE(rubik::zz::is_zzf2l_right_solved(r));
  // R and L alone preserve EOLine (EO + DF/DB), a ZZ key property.
  EXPECT_TRUE(rubik::zz::is_eoline_solved(r));
  EXPECT_TRUE(rubik::zz::is_eoline_solved(l));
}

TEST(Zz, RluPreservesEoline) {
  // Every R/L/U token applied to an EOLine-solved state keeps EOLine.
  const CubeState solved = CubeState::solved();
  const std::vector<std::string> zz = rubik::zz::zzf2l_allowed_moves();
  for (const std::string& tok : zz) {
    const CubeState after = applyTokens(solved, {tok});
    EXPECT_TRUE(rubik::zz::is_eoline_solved(after))
        << "R/L/U token breaks EOLine: " << tok;
  }
  // A longer R/L/U sequence from solved also keeps EOLine (it never
  // reaches F/B moves, the only edge-flipping face moves).
  CubeState cur = solved;
  const std::vector<std::string> seq = {"R", "U", "L'", "R'", "U2",
                                        "L",  "U'", "R2", "L2", "U"};
  for (const std::string& tok : seq) {
    cur = applyTokens(cur, {tok});
    EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
        << "EOLine broken by token " << tok;
    EXPECT_TRUE(isZzToken(tok));
  }
}

TEST(Zz, SolvedCube) {
  auto res = rubik::zz::solve(CubeState::solved(), defaultOptions());
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->total_moves(), 0);
  expectMoveSets(*res);
  expectStagePredicates(CubeState::solved(), *res);
}

TEST(Zz, ShallowScrambles) {
  const std::vector<std::string> scrambles = {
      "R",
      "R U",
      "R U R' U'",
      "F R U R' U' F'",
      "R U R' U R U2 R'",
  };
  for (const std::string& text : scrambles) {
    const CubeState state = scrambleStateText(text);
    auto res = rubik::zz::solve(state, defaultOptions());
    ASSERT_TRUE(res.has_value())
        << "scramble: " << text
        << " error: " << static_cast<int>(res.error());
    expectMoveSets(*res);
    expectStagePredicates(state, *res);
    auto parsed = rubik::parse_moves(text);
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> scrambleTokens;
    for (const Move& m : *parsed) scrambleTokens.push_back(m.token);
    std::string f = refsim::apply_sequence(refsim::solved_facelets(),
                                           scrambleTokens);
    f = refsim::apply_sequence(f, solutionTokens(*res));
    EXPECT_EQ(f, refsim::solved_facelets()) << "scramble: " << text;
    EXPECT_EQ(rubik::to_facelets(state),
              refsim::apply_sequence(refsim::solved_facelets(), scrambleTokens))
        << "core vs refsim on scramble: " << text;
  }
}

TEST(Zz, RandomDepth25) {
  std::mt19937 rng(20261108u);
  const int kTrials = 5;
  for (int trial = 0; trial < kTrials; ++trial) {
    const std::vector<std::string> scramble = randomScrambleTokens(rng, 25);
    const CubeState state = scrambleState(scramble);
    EXPECT_EQ(rubik::to_facelets(state),
              refsim::apply_sequence(refsim::solved_facelets(), scramble))
        << "core vs refsim, trial " << trial;
    auto res = rubik::zz::solve(state, defaultOptions());
    ASSERT_TRUE(res.has_value())
        << "trial " << trial << " scramble: " << joinTokens(scramble)
        << " error: "
        << (res.has_value() ? "none"
                            : std::to_string(static_cast<int>(res.error())));
    expectMoveSets(*res);
    expectStagePredicates(state, *res);
    // R/L/U tokens inside ZZF2L stages preserve EOLine step by step.
    CubeState cur = state;
    cur = applyTokens(cur, res->stages[0].moves);
    ASSERT_TRUE(rubik::zz::is_eoline_solved(cur));
    for (std::size_t s = 1; s <= 2; ++s) {
      for (const std::string& tok : res->stages[s].moves) {
        EXPECT_TRUE(isZzToken(tok));
        cur = applyTokens(cur, {tok});
        EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
            << "ZZF2L token " << tok << " breaks EOLine";
      }
    }
    std::string f = refsim::apply_sequence(refsim::solved_facelets(), scramble);
    f = refsim::apply_sequence(f, solutionTokens(*res));
    EXPECT_EQ(f, refsim::solved_facelets())
        << "trial " << trial << " scramble: " << joinTokens(scramble)
        << " solution: " << joinTokens(solutionTokens(*res));
    std::cout << std::format("zz trial {}: {} moves\n", trial,
                             res->total_moves());
  }
}

TEST(Zz, TimeoutBehavior) {
  const CubeState state = scrambleStateText("R U F R' U' F' R U2 R' L' U L");
  SolveOptions opts;
  opts.timeout_ms = 1;
  opts.max_depth = 30;
  opts.thread_count = 1;
  auto res = rubik::zz::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Zz, CancelledBehavior) {
  const CubeState state = scrambleStateText("R U");
  std::stop_source src;
  src.request_stop();
  SolveOptions opts;
  opts.stop_token = src.get_token();
  opts.timeout_ms = 120000;
  opts.max_depth = 30;
  opts.thread_count = 1;
  auto res = rubik::zz::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Cancelled);
}

TEST(Zz, UnsolvableInvalidParity) {
  CubeState bad = CubeState::solved();
  std::swap(bad.cp[0], bad.cp[1]);
  auto res = rubik::zz::solve(bad, defaultOptions());
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Unsolvable);
}

TEST(Zz, MaxDepthRespected) {
  const CubeState state = scrambleStateText("R U F");
  SolveOptions opts;
  opts.timeout_ms = 120000;
  opts.max_depth = 0;
  opts.thread_count = 1;
  auto res = rubik::zz::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Zz, DifferentialMoveCountNoRegression) {
  // Differential / no-regression test: a deterministic batch of depth-25
  // scrambles, all solved and verified against the independent reference
  // simulator, with move counts bounded by headroom over the observed
  // baseline average (~48 moves).
  //
  // setMovePruning only removes provably-redundant or equal-cost move
  // sequences (two same-face turns compose to <=1 turn; R and L commute on
  // disjoint cubies so RL == LR, and only ONE order of the R/L opposite
  // pair is pruned -- never both).  Optimal stage length is therefore
  // preserved: solutions must not get longer, and must always solve.
  std::mt19937 rng(20261108u);
  const int kTrials = 25;
  const int kMaxMoves = 80;
  int max_moves = 0;
  for (int trial = 0; trial < kTrials; ++trial) {
    const std::vector<std::string> scramble = randomScrambleTokens(rng, 25);
    const CubeState state = scrambleState(scramble);
    auto res = rubik::zz::solve(state, defaultOptions());
    ASSERT_TRUE(res.has_value())
        << "trial " << trial << " scramble: " << joinTokens(scramble)
        << " error: " << static_cast<int>(res.error());
    expectMoveSets(*res);
    expectStagePredicates(state, *res);
    const int moves = res->total_moves();
    max_moves = (std::max)(max_moves, moves);
    EXPECT_LE(moves, kMaxMoves)
        << "trial " << trial << " scramble: " << joinTokens(scramble);
    std::string f = refsim::apply_sequence(refsim::solved_facelets(), scramble);
    f = refsim::apply_sequence(f, solutionTokens(*res));
    EXPECT_EQ(f, refsim::solved_facelets())
        << "trial " << trial << " scramble: " << joinTokens(scramble);
  }
  std::cout << std::format("zz differential: {} scrambles, max moves {}\n",
                           kTrials, max_moves);
}

TEST(Zz, Zzf2lPreservesEolineAndBlocks) {
  const CubeState state = scrambleStateText("R U2 F' D L B' U R2 F");
  auto res = rubik::zz::solve(state, defaultOptions());
  ASSERT_TRUE(res.has_value());
  expectMoveSets(*res);
  CubeState cur = state;
  cur = applyTokens(cur, res->stages[0].moves);
  EXPECT_TRUE(rubik::zz::is_eoline_solved(cur));
  // Left stage: every token keeps EOLine.
  for (const std::string& tok : res->stages[1].moves) {
    EXPECT_TRUE(isZzToken(tok));
    cur = applyTokens(cur, {tok});
    EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
        << "left token " << tok << " breaks EOLine";
  }
  EXPECT_TRUE(rubik::zz::is_zzf2l_left_solved(cur));
  // Right stage: every token keeps EOLine; final state has both blocks.
  for (const std::string& tok : res->stages[2].moves) {
    EXPECT_TRUE(isZzToken(tok));
    cur = applyTokens(cur, {tok});
    EXPECT_TRUE(rubik::zz::is_eoline_solved(cur))
        << "right token " << tok << " breaks EOLine";
  }
  EXPECT_TRUE(rubik::zz::is_zzf2l_solved(cur));
  EXPECT_TRUE(rubik::is_solved(applyTokens(
      applyTokens(cur, res->stages[3].moves), res->stages[4].moves)));
}

TEST(Zz, AdmissibilityGoalPlusK) {
  // A stage PDB heuristic must be admissible: h(s) <= true stage distance to
  // the stage goal. From the solved state, k random stage-allowed moves reach
  // a state at distance <= k, so h(s) <= k for every k in 0..6.
  const std::vector<std::string> stages = rubik::zz::stage_names();
  std::mt19937 rng(20261108u);
  for (const std::string& stage : stages) {
    const std::vector<std::string> allowed =
        rubik::zz::allowed_moves_for_stage(stage);
    ASSERT_FALSE(allowed.empty()) << "stage " << stage << " has no allowed moves";
    for (int k = 0; k <= 6; ++k) {
      CubeState s = CubeState::solved();
      for (int i = 0; i < k; ++i) {
        s = applyTokens(s, {allowed[static_cast<std::size_t>(rng()) %
                                       allowed.size()]});
      }
      const int h = rubik::zz::stageHeuristicForTest(stage, s);
      EXPECT_LE(h, k) << stage << " k=" << k;
    }
  }
}
