// Differential test: CFOP optimized solver vs. CFOP baseline.
//
// For every scramble (fixed seed, depth 25) the optimized solver must:
//
//   1. Always solve (100% verified success).
//   2. Use no more moves than the baseline CFOP solver for the *same*
//      scramble (move count <= baseline per scramble).
//
// The baseline move counts below were captured by solving the identical
// scrambles (same seed 20261108, same scramble generator) with the
// pre-optimization CFOP solver (git HEAD, before the Korf stage-PDB /
// move-pruning upgrade).  Because move pruning is lossless and both
// solvers run IDA* to per-stage optimality with admissible heuristics,
// optimized move counts equal (never exceed) baseline move counts.
//
// See docs/SPEC.md Section 7 (CFOP) and the acceptance criterion F-06.
#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <random>
#include <stop_token>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "../core/reference_sim.hpp"
#include "solvers/cfop/solver.hpp"

namespace {

using rubik::CubeState;
using rubik::Move;
using rubik::SolveError;
using rubik::SolveOptions;
using rubik::Solution;

constexpr int kTrials = 20;
constexpr int kDepth = 25;
constexpr unsigned kSeedBase = 20261108u;

// Baseline total move counts for the kTrials scrambles generated with
// seed kSeedBase and depth kDepth, captured from the pre-upgrade CFOP
// solver.  Indexed by trial; must match the test's scramble generation
// exactly (same seed, same generator, same move pool).
constexpr std::array<int, kTrials> kBaselineMoves = {{
    51, 57, 55, 52, 47, 57, 51, 49, 49, 56,
    55, 55, 55, 59, 55, 59, 46, 52, 58, 59,
}};

static_assert(kBaselineMoves.size() == static_cast<std::size_t>(kTrials),
              "baseline move table must cover every trial");

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
      pick = pool[static_cast<std::size_t>(rng()) % pool.size()];
    } while (!out.empty() && faceOf(pick) == faceOf(out.back()));
    out.push_back(pick);
  }
  return out;
}

CubeState scrambleState(const std::vector<std::string>& tokens) {
  std::vector<Move> moves;
  moves.reserve(tokens.size());
  for (const std::string& t : tokens) moves.push_back(Move{t});
  return rubik::apply(CubeState::solved(), moves);
}

std::vector<std::string> solutionTokens(const Solution& sol) {
  std::vector<std::string> out;
  for (const auto& stage : sol.stages) {
    for (const std::string& t : stage.moves) out.push_back(t);
  }
  return out;
}

std::string joinTokens(const std::vector<std::string>& tokens) {
  std::string out;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i != 0) out += ' ';
    out += tokens[i];
  }
  return out;
}

SolveOptions defaultOptions() {
  SolveOptions opts;
  opts.timeout_ms = 30000;
  opts.max_depth = 30;
  opts.thread_count = 1;
  return opts;
}

// Reference-simulator check: solved + scramble + solution == solved.
bool verifySolution(const std::vector<std::string>& scramble,
                    const Solution& sol) {
  std::string f = refsim::apply_sequence(refsim::solved_facelets(), scramble);
  f = refsim::apply_sequence(f, solutionTokens(sol));
  return f == refsim::solved_facelets();
}

}  // namespace

TEST(CfopDifferential, SolvesAndMoveCountNotRegressedVsBaseline) {
  std::mt19937 rng(kSeedBase);
  int successes = 0;
  int totalOptMoves = 0;
  int totalBaselineMoves = 0;

  for (int trial = 0; trial < kTrials; ++trial) {
    const std::vector<std::string> scramble =
        randomScrambleTokens(rng, kDepth);
    const CubeState state = scrambleState(scramble);

    auto res = rubik::cfop::solve(state, defaultOptions());
    SCOPED_TRACE(std::format("trial {} scramble {}", trial,
                             joinTokens(scramble)));

    // F-06(a): the optimized solver always solves.
    ASSERT_TRUE(res.has_value())
        << "trial " << trial << " failed to solve";

    // Independent verification via the reference simulator.
    EXPECT_TRUE(verifySolution(scramble, *res))
        << "trial " << trial << " solution does not restore solved state";

    const int optMoves = res->total_moves();
    const int baseMoves = kBaselineMoves[static_cast<std::size_t>(trial)];
    ++successes;
    totalOptMoves += optMoves;
    totalBaselineMoves += baseMoves;

    // F-06(b): move count <= baseline per scramble.
    EXPECT_LE(optMoves, baseMoves)
        << "trial " << trial << " optimized moves=" << optMoves
        << " > baseline moves=" << baseMoves;

    std::cout << std::format("cfop diff trial {}: opt={} baseline={} scramble={}\n",
                             trial, optMoves, baseMoves,
                             joinTokens(scramble));
  }

  // Aggregate: 100% verified success and no average-moves regression.
  EXPECT_EQ(successes, kTrials);
  EXPECT_LE(totalOptMoves, totalBaselineMoves)
      << "aggregate optimized moves=" << totalOptMoves
      << " > baseline moves=" << totalBaselineMoves;
}
