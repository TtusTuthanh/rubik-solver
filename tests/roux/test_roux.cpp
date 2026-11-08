// Tests for rubik::roux (First block -> Second block -> CMLL -> LSE).
// See docs/SPEC.md Sections 7 and 10.
//
// Coverage:
//  - stage order and restricted move sets (esp. LSE M/U only),
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
#include "solvers/roux/solver.hpp"
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

bool isLseToken(const std::string& t) {
  return t == "M" || t == "M'" || t == "M2" || t == "U" || t == "U'" ||
         t == "U2";
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
  ASSERT_EQ(sol.stages.size(), 4u);
  cur = applyTokens(cur, sol.stages[0].moves);
  EXPECT_TRUE(rubik::roux::is_first_block_solved(cur))
      << "first_block predicate after stage 0";
  cur = applyTokens(cur, sol.stages[1].moves);
  EXPECT_TRUE(rubik::roux::is_first_block_solved(cur))
      << "first_block preserved after second_block";
  EXPECT_TRUE(rubik::roux::is_second_block_solved(cur))
      << "second_block predicate after stage 1";
  cur = applyTokens(cur, sol.stages[2].moves);
  EXPECT_TRUE(rubik::roux::is_second_block_solved(cur))
      << "second_block preserved after cmll";
  EXPECT_TRUE(rubik::roux::is_cmll_solved(cur)) << "cmll predicate";
  cur = applyTokens(cur, sol.stages[3].moves);
  EXPECT_TRUE(rubik::roux::is_lse_solved(cur)) << "lse predicate";
  EXPECT_TRUE(rubik::roux::is_cmll_solved(cur))
      << "cmll preserved (up to U) after lse";
  EXPECT_TRUE(rubik::is_solved(cur)) << "final state solved";
}

void expectMoveSets(const Solution& sol) {
  const std::vector<std::string> expected = rubik::roux::stage_names();
  ASSERT_EQ(sol.stages.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(sol.stages[i].stage_name, expected[i])
        << "stage order mismatch at index " << i;
    const std::vector<std::string> allowed =
        rubik::roux::allowed_moves_for_stage(expected[i]);
    for (const std::string& tok : sol.stages[i].moves) {
      EXPECT_TRUE(contains(allowed, tok))
          << "token " << tok << " not allowed in stage " << expected[i];
      EXPECT_TRUE(rubik::is_valid_move_token(tok)) << "invalid token " << tok;
    }
  }
  // LSE must use only M and U moves.
  for (const std::string& tok : sol.stages[3].moves) {
    EXPECT_TRUE(isLseToken(tok)) << "LSE uses non-M/U token: " << tok;
  }
}

}  // namespace

TEST(Roux, StageNamesAndMoveSets) {
  const std::vector<std::string> names = rubik::roux::stage_names();
  const std::vector<std::string> expected = {"first_block", "second_block",
                                             "cmll", "lse"};
  EXPECT_EQ(names, expected);
  EXPECT_EQ(rubik::roux::first_block_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::roux::second_block_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::roux::cmll_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::roux::lse_allowed_moves().size(), 6u);
  const std::vector<std::string> lse = rubik::roux::lse_allowed_moves();
  for (const std::string& tok : lse) {
    EXPECT_TRUE(isLseToken(tok)) << "LSE allowed has non-M/U token: " << tok;
  }
  EXPECT_TRUE(contains(lse, "M"));
  EXPECT_TRUE(contains(lse, "U"));
  EXPECT_EQ(rubik::roux::allowed_moves_for_stage("first_block").size(), 18u);
  EXPECT_EQ(rubik::roux::allowed_moves_for_stage("cmll").size(), 18u);
  EXPECT_EQ(rubik::roux::allowed_moves_for_stage("lse").size(), 6u);
  EXPECT_EQ(rubik::roux::allowed_moves_for_stage("unknown").size(), 0u);
}

TEST(Roux, PredicatesOnSolved) {
  const CubeState solved = CubeState::solved();
  EXPECT_TRUE(rubik::roux::is_first_block_solved(solved));
  EXPECT_TRUE(rubik::roux::is_second_block_solved(solved));
  EXPECT_TRUE(rubik::roux::is_cmll_solved(solved));
  EXPECT_TRUE(rubik::roux::is_lse_solved(solved));
  const CubeState r = scrambleStateText("L");
  EXPECT_FALSE(rubik::roux::is_first_block_solved(r));
  EXPECT_FALSE(rubik::roux::is_second_block_solved(r));
  EXPECT_FALSE(rubik::roux::is_cmll_solved(r));
  EXPECT_FALSE(rubik::roux::is_lse_solved(r));
  // A U turn from solved keeps both blocks and the CMLL predicate
  // (U corners are a U rotation of solved) but is not solved.
  const CubeState u = scrambleStateText("U");
  EXPECT_TRUE(rubik::roux::is_first_block_solved(u));
  EXPECT_TRUE(rubik::roux::is_second_block_solved(u));
  EXPECT_TRUE(rubik::roux::is_cmll_solved(u));
  EXPECT_FALSE(rubik::roux::is_lse_solved(u));
  const CubeState u2 = scrambleStateText("U2");
  EXPECT_TRUE(rubik::roux::is_cmll_solved(u2));
  EXPECT_FALSE(rubik::roux::is_lse_solved(u2));
}

TEST(Roux, SolvedCube) {
  auto res = rubik::roux::solve(CubeState::solved(), defaultOptions());
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->total_moves(), 0);
  expectMoveSets(*res);
  expectStagePredicates(CubeState::solved(), *res);
}

TEST(Roux, ShallowScrambles) {
  const std::vector<std::string> scrambles = {
      "R",
      "R U",
      "R U R' U'",
      "F R U R' U' F'",
      "R U R' U R U2 R'",
  };
  for (const std::string& text : scrambles) {
    const CubeState state = scrambleStateText(text);
    auto res = rubik::roux::solve(state, defaultOptions());
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

TEST(Roux, RandomDepth25) {
  std::mt19937 rng(20261108u);
  const int kTrials = 5;
  for (int trial = 0; trial < kTrials; ++trial) {
    const std::vector<std::string> scramble = randomScrambleTokens(rng, 25);
    const CubeState state = scrambleState(scramble);
    EXPECT_EQ(rubik::to_facelets(state),
              refsim::apply_sequence(refsim::solved_facelets(), scramble))
        << "core vs refsim, trial " << trial;
    auto res = rubik::roux::solve(state, defaultOptions());
    ASSERT_TRUE(res.has_value())
        << "trial " << trial << " scramble: " << joinTokens(scramble)
        << " error: "
        << (res.has_value() ? "none"
                            : std::to_string(static_cast<int>(res.error())));
    expectMoveSets(*res);
    expectStagePredicates(state, *res);
    std::string f = refsim::apply_sequence(refsim::solved_facelets(), scramble);
    f = refsim::apply_sequence(f, solutionTokens(*res));
    EXPECT_EQ(f, refsim::solved_facelets())
        << "trial " << trial << " scramble: " << joinTokens(scramble)
        << " solution: " << joinTokens(solutionTokens(*res));
    std::cout << std::format("roux trial {}: {} moves\n", trial,
                             res->total_moves());
  }
}

TEST(Roux, TimeoutBehavior) {
  const CubeState state = scrambleStateText("R U F R' U' F' R U2 R' L' U L");
  SolveOptions opts;
  opts.timeout_ms = 1;
  opts.max_depth = 30;
  opts.thread_count = 1;
  auto res = rubik::roux::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Roux, CancelledBehavior) {
  const CubeState state = scrambleStateText("R U");
  std::stop_source src;
  src.request_stop();
  SolveOptions opts;
  opts.stop_token = src.get_token();
  opts.timeout_ms = 120000;
  opts.max_depth = 30;
  opts.thread_count = 1;
  auto res = rubik::roux::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Cancelled);
}

TEST(Roux, UnsolvableInvalidParity) {
  CubeState bad = CubeState::solved();
  std::swap(bad.cp[0], bad.cp[1]);
  auto res = rubik::roux::solve(bad, defaultOptions());
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Unsolvable);
}

TEST(Roux, MaxDepthRespected) {
  const CubeState state = scrambleStateText("R U F");
  SolveOptions opts;
  opts.timeout_ms = 120000;
  opts.max_depth = 0;
  opts.thread_count = 1;
  auto res = rubik::roux::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Roux, LseOnlyMuAndPreservesBlocks) {
  const CubeState state = scrambleStateText("R U2 F' D L B' U R2 F");
  auto res = rubik::roux::solve(state, defaultOptions());
  ASSERT_TRUE(res.has_value());
  expectMoveSets(*res);
  // Applying only the LSE stage from the CMLL state must preserve both
  // blocks (M/U fix all block cubies) and solve the cube.
  CubeState cur = state;
  for (std::size_t i = 0; i < 3; ++i) {
    cur = applyTokens(cur, res->stages[i].moves);
  }
  EXPECT_TRUE(rubik::roux::is_cmll_solved(cur));
  for (const std::string& tok : res->stages[3].moves) {
    EXPECT_TRUE(isLseToken(tok));
    cur = applyTokens(cur, {tok});
    EXPECT_TRUE(rubik::roux::is_first_block_solved(cur))
        << "first_block broken by LSE token " << tok;
    EXPECT_TRUE(rubik::roux::is_second_block_solved(cur))
        << "second_block broken by LSE token " << tok;
  }
  EXPECT_TRUE(rubik::is_solved(cur));
}
