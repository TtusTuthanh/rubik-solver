// Admissibility test for the Roux Korf-style PDB heuristic.
//
// For each stage (first_block, second_block, cmll, lse), we start from the
// stage goal state(s) and apply k random allowed moves (k = 0..6). The true
// stage distance of the resulting state is at most k (reverse the applied
// moves), so an admissible heuristic must satisfy h(state) <= k.
//
// Uses the public heuristicForStage() accessor (see solver.hpp), which is
// backed by the solver's real PDBs built by buildPdbs(). This exercises the
// exact heuristic the solver uses.
//
// See docs/SPEC.md Section 7 (admissibility: h <= true stage distance).

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "solvers/roux/solver.hpp"

using rubik::CubeState;
using rubik::Move;
using rubik::apply;

namespace {

// Apply `depth` random moves from `start`, avoiding immediate same-face
// reversals, and return the resulting state. Moves are drawn from the stage's
// allowed move set.
CubeState randomWalkFromGoal(const CubeState& start,
                             const std::vector<std::string>& allowed,
                             int depth,
                             std::mt19937& rng) {
  CubeState state = start;
  int last_face = -1;
  for (int i = 0; i < depth; ++i) {
    std::size_t idx;
    int face;
    do {
      idx = static_cast<std::size_t>(rng() % allowed.size());
      // Group consecutive moves of the same face (idx/3 works for both the
      // 18 face moves and the 6 LSE moves: M*/U*).
      face = static_cast<int>(idx / 3u);
    } while (face == last_face);
    last_face = face;
    state = apply(state, Move{allowed[idx]});
  }
  return state;
}

std::vector<std::string> allowedFor(const std::string& stage) {
  return rubik::roux::allowed_moves_for_stage(stage);
}

// Goal state(s) per stage. CMLL accepts the 4 U rotations of the solved U
// corners (FB+SB solved); all other stages use the solved state.
std::vector<CubeState> goalStates(const std::string& stage) {
  if (stage == "cmll") {
    std::vector<CubeState> goals;
    goals.reserve(4);
    for (int k = 0; k < 4; ++k) {
      CubeState g = CubeState::solved();
      for (int i = 0; i < k; ++i) g = apply(g, Move{"U"});
      goals.push_back(std::move(g));
    }
    return goals;
  }
  return {CubeState::solved()};
}

// Admissibility check: h(state) <= k for states k moves (k = 0..6) from each
// goal state. trials_per_k random states are sampled per (goal, k).
void checkStageAdmissible(const std::string& stage,
                          const std::vector<std::string>& allowed,
                          int trials_per_k) {
  std::mt19937 rng(20261108u);
  const auto goals = goalStates(stage);
  for (const auto& goal : goals) {
    for (int k = 0; k <= 6; ++k) {
      for (int t = 0; t < trials_per_k; ++t) {
        const CubeState state = randomWalkFromGoal(goal, allowed, k, rng);
        const int h = rubik::roux::heuristicForStage(stage, state);
        EXPECT_LE(h, k)
            << "stage=" << stage << " k=" << k << " trial=" << t
            << " h=" << h << " > k (heuristic inadmissible)";
      }
    }
  }
}

TEST(RouxAdmissibility, FirstBlockAdmissible) {
  checkStageAdmissible("first_block", allowedFor("first_block"), 50);
}

TEST(RouxAdmissibility, SecondBlockAdmissible) {
  checkStageAdmissible("second_block", allowedFor("second_block"), 50);
}

TEST(RouxAdmissibility, CmllAdmissible) {
  checkStageAdmissible("cmll", allowedFor("cmll"), 50);
}

TEST(RouxAdmissibility, LseAdmissible) {
  checkStageAdmissible("lse", allowedFor("lse"), 50);
}

}  // namespace
