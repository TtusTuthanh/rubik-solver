// Admissibility tests for the CFOP per-stage heuristics.
//
// For every stage, h(state) must be <= the true stage distance.  We check the
// necessary condition "goal + k random allowed moves (k=0..6) => h <= k":
// applying k moves to the solved goal yields a state whose true stage distance
// is at most k (undoing those k moves solves it), so an admissible heuristic
// can never exceed k.  This catches index/move-table bugs that would make a
// PDB overestimate.  The PDBs themselves are built by exact multi-source BFS
// from the stage goal (so h <= true distance for ALL states); this test is a
// spot-check of the lookup wiring.
//
// Style mirrors the per-method admissibility convention (goal + k random
// moves, k=0..6) used across the human-method solvers.
#include <gtest/gtest.h>

#include <array>
#include <random>
#include <string>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "solvers/cfop/solver.hpp"

namespace {

using rubik::CubeState;
using rubik::Move;
using rubik::SolveOptions;
using rubik::cfop::TestStageKind;
using rubik::cfop::allowed_moves_for_stage;
using rubik::cfop::cross_allowed_moves;
using rubik::cfop::f2l_allowed_moves;
using rubik::cfop::oll_allowed_moves;
using rubik::cfop::pll_allowed_moves;
using rubik::cfop::testStageHeuristic;
using rubik::cfop::is_cross_solved;
using rubik::cfop::is_f2l_solved;
using rubik::cfop::is_oll_solved;
using rubik::cfop::is_pll_solved;

std::mt19937& rng() {
  static std::mt19937 g(20261108u);
  return g;
}

// Apply k random moves from `pool` to `start`, avoiding immediate same-face
// repeats (mirrors the fuzz scramble generator).
CubeState randomWalk(const CubeState& start,
                     const std::vector<std::string>& pool, int k) {
  CubeState s = start;
  std::string lastFace;
  for (int i = 0; i < k; ++i) {
    std::string tok;
    do {
      tok = pool[static_cast<std::size_t>(rng()() % pool.size())];
    } while (!lastFace.empty() && !tok.empty() && tok[0] == lastFace[0]);
    lastFace = tok.empty() ? std::string{} : std::string(1, tok[0]);
    s = rubik::apply(s, std::vector<Move>{Move{tok}});
  }
  return s;
}

std::vector<double> ms;

TEST(CfopAdmissibility, CrossNearGoal) {
  const auto pool = cross_allowed_moves();
  const int trials = 300;
  for (int trial = 0; trial < trials; ++trial) {
    const int k = static_cast<int>(rng()() % 7u);  // 0..6
    const CubeState s = randomWalk(CubeState::solved(), pool, k);
    const int h = testStageHeuristic(TestStageKind::Cross, 0, s);
    EXPECT_LE(h, k) << "cross h=" << h << " > k=" << k;
  }
}

TEST(CfopAdmissibility, F2L_1NearGoal) {
  const auto pool = f2l_allowed_moves();
  const int trials = 300;
  for (int trial = 0; trial < trials; ++trial) {
    const int k = static_cast<int>(rng()() % 7u);
    const CubeState s = randomWalk(CubeState::solved(), pool, k);
    const int h = testStageHeuristic(TestStageKind::F2L_1, 0, s);
    EXPECT_LE(h, k) << "f2l_1 h=" << h << " > k=" << k;
  }
}

TEST(CfopAdmissibility, OllNearGoal) {
  const auto pool = oll_allowed_moves();
  const int trials = 300;
  for (int trial = 0; trial < trials; ++trial) {
    const int k = static_cast<int>(rng()() % 7u);
    const CubeState s = randomWalk(CubeState::solved(), pool, k);
    const int h = testStageHeuristic(TestStageKind::Oll, 0, s);
    EXPECT_LE(h, k) << "oll h=" << h << " > k=" << k;
  }
}

TEST(CfopAdmissibility, PllNearGoal) {
  const auto pool = pll_allowed_moves();
  const int trials = 300;
  for (int trial = 0; trial < trials; ++trial) {
    const int k = static_cast<int>(rng()() % 7u);
    const CubeState s = randomWalk(CubeState::solved(), pool, k);
    const int h = testStageHeuristic(TestStageKind::Pll, 0, s);
    EXPECT_LE(h, k) << "pll h=" << h << " > k=" << k;
  }
}

// Sanity: the heuristic is exact (==0) at the goal and stays admissible when
// the goal predicate holds.
TEST(CfopAdmissibility, HeuristicZeroAtGoal) {
  const CubeState solved = CubeState::solved();
  EXPECT_EQ(testStageHeuristic(TestStageKind::Cross, 0, solved), 0);
  EXPECT_EQ(testStageHeuristic(TestStageKind::F2L_1, 0, solved), 0);
  EXPECT_EQ(testStageHeuristic(TestStageKind::Oll, 0, solved), 0);
  EXPECT_EQ(testStageHeuristic(TestStageKind::Pll, 0, solved), 0);
}

// OLL drop of cross15/pair15 must not weaken admissibility: on random OLL
// states near goal the heuristic stays <= k (cross15/pair15 were 0 anyway).
TEST(CfopAdmissibility, OllDroppedTermsStillAdmissible) {
  const auto pool = oll_allowed_moves();
  const int trials = 300;
  for (int trial = 0; trial < trials; ++trial) {
    const int k = static_cast<int>(rng()() % 7u);
    const CubeState s = randomWalk(CubeState::solved(), pool, k);
    const int h = testStageHeuristic(TestStageKind::Oll, 0, s);
    EXPECT_LE(h, k) << "oll(dropped) h=" << h << " > k=" << k;
  }
}

}  // namespace
