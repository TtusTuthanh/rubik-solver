// Tests for rubik::heise (squares -> F2L-1 -> corner-edge pairs -> final corners).
// See docs/methods/heise.md and docs/SPEC.md Sections 7 and 10.
//
// Coverage:
//  - stage order and restricted move sets (all 18 face moves per stage),
//  - stage predicates after each stage,
//  - nesting P4 => P3 => P2 => P1 on solved and scrambled states,
//  - random scrambles of depth 25 solved and verified via the independent
//    reference simulator (tests/core/reference_sim.hpp),
//  - timeout / cancelled / unsolvable / max-depth behaviour.
#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <random>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "solvers/heise/solver.hpp"
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
  EXPECT_TRUE(rubik::heise::is_squares_solved(cur))
      << "squares predicate after stage 0";
  cur = applyTokens(cur, sol.stages[1].moves);
  EXPECT_TRUE(rubik::heise::is_squares_solved(cur))
      << "squares preserved after f2l_minus_1";
  EXPECT_TRUE(rubik::heise::is_f2l_minus_1_solved(cur))
      << "f2l_minus_1 predicate after stage 1";
  cur = applyTokens(cur, sol.stages[2].moves);
  EXPECT_TRUE(rubik::heise::is_squares_solved(cur))
      << "squares preserved after corner_edge_pairs";
  EXPECT_TRUE(rubik::heise::is_f2l_minus_1_solved(cur))
      << "f2l_minus_1 preserved after corner_edge_pairs";
  EXPECT_TRUE(rubik::heise::is_corner_edge_pairs_solved(cur))
      << "corner_edge_pairs predicate after stage 2";
  cur = applyTokens(cur, sol.stages[3].moves);
  EXPECT_TRUE(rubik::heise::is_final_corners_solved(cur))
      << "final_corners predicate";
  EXPECT_TRUE(rubik::heise::is_corner_edge_pairs_solved(cur))
      << "corner_edge_pairs preserved after final_corners";
  EXPECT_TRUE(rubik::is_solved(cur)) << "final state solved";
}

void expectMoveSets(const Solution& sol) {
  const std::vector<std::string> expected = rubik::heise::stage_names();
  ASSERT_EQ(sol.stages.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(sol.stages[i].stage_name, expected[i])
        << "stage order mismatch at index " << i;
    const std::vector<std::string> allowed =
        rubik::heise::allowed_moves_for_stage(expected[i]);
    for (const std::string& tok : sol.stages[i].moves) {
      EXPECT_TRUE(contains(allowed, tok))
          << "token " << tok << " not allowed in stage " << expected[i];
      EXPECT_TRUE(rubik::is_valid_move_token(tok)) << "invalid token " << tok;
    }
  }
  // Heise uses face moves only: no slices or rotations in any stage.
  for (const auto& stage : sol.stages) {
    for (const std::string& tok : stage.moves) {
      const char base = tok.empty() ? '?' : tok[0];
      EXPECT_TRUE(base == 'U' || base == 'R' || base == 'F' || base == 'D' ||
                  base == 'L' || base == 'B')
          << "non-face token " << tok << " in stage " << stage.stage_name;
    }
  }
}

}  // namespace

TEST(Heise, StageNamesAndMoveSets) {
  const std::vector<std::string> names = rubik::heise::stage_names();
  const std::vector<std::string> expected = {"squares", "f2l_minus_1",
                                             "corner_edge_pairs", "final_corners"};
  EXPECT_EQ(names, expected);
  EXPECT_EQ(rubik::heise::squares_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::heise::f2l_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::heise::corner_edge_pairs_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::heise::final_corners_allowed_moves().size(), 18u);
  EXPECT_EQ(rubik::heise::allowed_moves_for_stage("squares").size(), 18u);
  EXPECT_EQ(rubik::heise::allowed_moves_for_stage("f2l_minus_1").size(), 18u);
  EXPECT_EQ(rubik::heise::allowed_moves_for_stage("corner_edge_pairs").size(),
            18u);
  EXPECT_EQ(rubik::heise::allowed_moves_for_stage("final_corners").size(), 18u);
  EXPECT_EQ(rubik::heise::allowed_moves_for_stage("unknown").size(), 0u);
}

TEST(Heise, PredicatesOnSolved) {
  const CubeState solved = CubeState::solved();
  EXPECT_TRUE(rubik::heise::is_squares_solved(solved));
  EXPECT_TRUE(rubik::heise::is_f2l_minus_1_solved(solved));
  EXPECT_TRUE(rubik::heise::is_corner_edge_pairs_solved(solved));
  EXPECT_TRUE(rubik::heise::is_final_corners_solved(solved));
  // Nesting on solved: all predicates hold.
  // F breaks D edges/corners, so every stage predicate fails.
  const CubeState f = scrambleStateText("F");
  EXPECT_FALSE(rubik::heise::is_squares_solved(f));
  EXPECT_FALSE(rubik::heise::is_f2l_minus_1_solved(f));
  EXPECT_FALSE(rubik::heise::is_corner_edge_pairs_solved(f));
  EXPECT_FALSE(rubik::heise::is_final_corners_solved(f));
  // U alone rotates the U layer: it preserves squares and F2L-1 (U edges stay
  // oriented, D/middle pieces untouched) but moves U edges out of place.
  const CubeState u = scrambleStateText("U");
  EXPECT_TRUE(rubik::heise::is_squares_solved(u));
  EXPECT_TRUE(rubik::heise::is_f2l_minus_1_solved(u));
  EXPECT_FALSE(rubik::heise::is_corner_edge_pairs_solved(u));
  EXPECT_FALSE(rubik::heise::is_final_corners_solved(u));
  // U2: same preservation story, also breaks corner_edge_pairs.
  const CubeState u2 = scrambleStateText("U2");
  EXPECT_TRUE(rubik::heise::is_squares_solved(u2));
  EXPECT_TRUE(rubik::heise::is_f2l_minus_1_solved(u2));
  EXPECT_FALSE(rubik::heise::is_corner_edge_pairs_solved(u2));
  // D breaks squares.
  const CubeState d = scrambleStateText("D");
  EXPECT_FALSE(rubik::heise::is_squares_solved(d));
  EXPECT_FALSE(rubik::heise::is_f2l_minus_1_solved(d));
}

TEST(Heise, PredicatesNestOnScramble) {
  // Nesting: P4 => P3 => P2 => P1 must hold for every state we probe.
  const std::vector<std::string> probes = {"R U", "F R U R' U' F'",
                                           "R U F D L B"};
  for (const std::string& text : probes) {
    const CubeState state = scrambleStateText(text);
    if (rubik::heise::is_final_corners_solved(state)) {
      EXPECT_TRUE(rubik::heise::is_corner_edge_pairs_solved(state)) << text;
      EXPECT_TRUE(rubik::heise::is_f2l_minus_1_solved(state)) << text;
      EXPECT_TRUE(rubik::heise::is_squares_solved(state)) << text;
    }
    if (rubik::heise::is_corner_edge_pairs_solved(state)) {
      EXPECT_TRUE(rubik::heise::is_f2l_minus_1_solved(state)) << text;
      EXPECT_TRUE(rubik::heise::is_squares_solved(state)) << text;
    }
    if (rubik::heise::is_f2l_minus_1_solved(state)) {
      EXPECT_TRUE(rubik::heise::is_squares_solved(state)) << text;
    }
  }
}

TEST(Heise, SolvedCube) {
  auto res = rubik::heise::solve(CubeState::solved(), defaultOptions());
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->total_moves(), 0);
  expectMoveSets(*res);
  expectStagePredicates(CubeState::solved(), *res);
}

TEST(Heise, ShallowScrambles) {
  const std::vector<std::string> scrambles = {
      "R",
      "R U",
      "R U R' U'",
      "F R U R' U' F'",
      "R U R' U R U2 R'",
  };
  for (const std::string& text : scrambles) {
    const CubeState state = scrambleStateText(text);
    auto res = rubik::heise::solve(state, defaultOptions());
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

TEST(Heise, RandomDepth25) {
  std::mt19937 rng(20261108u);
  const int kTrials = 5;
  for (int trial = 0; trial < kTrials; ++trial) {
    const std::vector<std::string> scramble = randomScrambleTokens(rng, 25);
    const CubeState state = scrambleState(scramble);
    EXPECT_EQ(rubik::to_facelets(state),
              refsim::apply_sequence(refsim::solved_facelets(), scramble))
        << "core vs refsim, trial " << trial;
    auto res = rubik::heise::solve(state, defaultOptions());
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
    std::cout << std::format("heise trial {}: {} moves\n", trial,
                             res->total_moves());
  }
}

TEST(Heise, TimeoutBehavior) {
  const CubeState state = scrambleStateText("R U F R' U' F' R U2 R' L' U L");
  SolveOptions opts;
  opts.timeout_ms = 1;
  opts.max_depth = 30;
  opts.thread_count = 1;
  auto res = rubik::heise::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Heise, CancelledBehavior) {
  const CubeState state = scrambleStateText("R U");
  std::stop_source src;
  src.request_stop();
  SolveOptions opts;
  opts.stop_token = src.get_token();
  opts.timeout_ms = 120000;
  opts.max_depth = 30;
  opts.thread_count = 1;
  auto res = rubik::heise::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Cancelled);
}

TEST(Heise, UnsolvableInvalidParity) {
  CubeState bad = CubeState::solved();
  std::swap(bad.cp[0], bad.cp[1]);
  auto res = rubik::heise::solve(bad, defaultOptions());
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Unsolvable);
}

TEST(Heise, MaxDepthRespected) {
  const CubeState state = scrambleStateText("R U F");
  SolveOptions opts;
  opts.timeout_ms = 120000;
  opts.max_depth = 0;
  opts.thread_count = 1;
  auto res = rubik::heise::solve(state, opts);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), SolveError::Timeout);
}

TEST(Heise, FinalCornersLeavesThreeCorners) {
  // From a corner_edge_pairs state (after stage 3 of a shallow solve), the 3
  // corners UFL,ULB,UBR are the only corners that may still be permuted.
  const CubeState state = scrambleStateText("R U2 F' D L B' U R2 F");
  auto res = rubik::heise::solve(state, defaultOptions());
  ASSERT_TRUE(res.has_value());
  expectMoveSets(*res);
  CubeState cur = state;
  for (std::size_t i = 0; i < 3; ++i) {
    cur = applyTokens(cur, res->stages[i].moves);
  }
  EXPECT_TRUE(rubik::heise::is_corner_edge_pairs_solved(cur));
  EXPECT_FALSE(rubik::heise::is_final_corners_solved(cur));
  // DRB and URF are solved; the final stage must sort UFL,ULB,UBR and finish.
  EXPECT_EQ(cur.cp[7], 7u);
  EXPECT_EQ(cur.cp[0], 0u);
  const std::vector<std::string> fin = res->stages[3].moves;
  CubeState done = cur;
  for (const std::string& tok : fin) {
    EXPECT_TRUE(rubik::is_valid_move_token(tok));
    done = applyTokens(done, {tok});
    // Every prefix of the final stage still has all edges solved only at the
    // end; intermediate states may scramble edges (commutator setup), so we
    // only require the final state to be solved.
  }
  EXPECT_TRUE(rubik::is_solved(done));
}

// ---------------------------------------------------------------------------
// Korf-style max-of-sums combinator for the pairing stage (substeps
// BrPair=5, Oll=10, UEdges=7, Pairs=8).
//
// For Heise's 18-face alphabet the SUM gate never passes: every face turns
// cubies from every group (move-support overlap), and f2l pairs share pieces
// with corner/edge groups (piece overlap).  Max-of-sums therefore collapses
// to MAX = stageHeuristicValue.  These tests pin that behavior: equality with
// the baseline heuristic (no behavior change), admissibility
// (h(goal + k moves) <= k for k = 0..6), and gate rejection on a state that
// maximizes every sub-PDB.
// ---------------------------------------------------------------------------

TEST(Heise, PairingStageHeuristicEqualsStageHeuristicValue) {
  const std::vector<rubik::heise::StageKind> kinds = {
      rubik::heise::StageKind::BrPair, rubik::heise::StageKind::Oll,
      rubik::heise::StageKind::UEdges, rubik::heise::StageKind::Pairs};
  // Solved state: every pairing-stage heuristic is 0 (goal).
  for (rubik::heise::StageKind kind : kinds) {
    EXPECT_EQ(rubik::heise::pairingStageHeuristicGroups(kind,
                                                        CubeState::solved()), 0);
    EXPECT_EQ(rubik::heise::stageHeuristicValue(kind, CubeState::solved()), 0);
  }
  // Random shallow states: combinator must equal the plain max heuristic.
  std::mt19937 rng(20261108u);
  for (int trial = 0; trial < 100; ++trial) {
    const auto tokens = randomScrambleTokens(rng, 8);
    const CubeState state = scrambleState(tokens);
    for (rubik::heise::StageKind kind : kinds) {
      EXPECT_EQ(rubik::heise::pairingStageHeuristicGroups(kind, state),
                rubik::heise::stageHeuristicValue(kind, state))
          << "kind=" << static_cast<int>(kind)
          << " scramble=" << joinTokens(tokens);
    }
  }
}

TEST(Heise, PairingStageAdmissibility) {
  // h(goal + k moves) <= k for k = 0..6.  The solved state satisfies every
  // stage goal; applying k moves yields a state at distance <= k from goal,
  // so an admissible heuristic must return <= k.
  const std::vector<rubik::heise::StageKind> kinds = {
      rubik::heise::StageKind::BrPair, rubik::heise::StageKind::Oll,
      rubik::heise::StageKind::UEdges, rubik::heise::StageKind::Pairs};
  std::mt19937 rng(20261109u);
  const auto& allMoves = rubik::all_face_moves();
  for (rubik::heise::StageKind kind : kinds) {
    for (int k = 0; k <= 6; ++k) {
      CubeState state = CubeState::solved();
      for (int m = 0; m < k; ++m) {
        const Move mv =
            allMoves[static_cast<std::size_t>(rng() % allMoves.size())];
        state = rubik::apply(state, {mv});
      }
      const int hGroup =
          rubik::heise::pairingStageHeuristicGroups(kind, state);
      const int hMax = rubik::heise::stageHeuristicValue(kind, state);
      EXPECT_LE(hGroup, k)
          << "kind=" << static_cast<int>(kind) << " k=" << k;
      EXPECT_LE(hMax, k)
          << "kind=" << static_cast<int>(kind) << " k=" << k;
    }
  }
}

TEST(Heise, PairingStageGateRejectsAllGroups) {
  // Structural admissibility gate: every pairing-stage group is MAX-ed under
  // Heise's 18-face alphabet.  Two patterns with full move support can never
  // be move-support-disjoint; f2l pairs also share pieces with corner/edge
  // groups.  Hence pairingStageHeuristicGroups == stageHeuristicValue on a
  // state constructed to maximize every sub-PDB (all heuristics > 0).
  std::mt19937 rng(20261110u);
  CubeState state = CubeState::solved();
  const auto& allMoves = rubik::all_face_moves();
  for (int m = 0; m < 25; ++m) {
    const Move mv =
        allMoves[static_cast<std::size_t>(rng() % allMoves.size())];
    state = rubik::apply(state, {mv});
  }
  for (rubik::heise::StageKind kind :
       {rubik::heise::StageKind::BrPair, rubik::heise::StageKind::Oll,
        rubik::heise::StageKind::UEdges, rubik::heise::StageKind::Pairs}) {
    EXPECT_EQ(rubik::heise::pairingStageHeuristicGroups(kind, state),
              rubik::heise::stageHeuristicValue(kind, state))
        << "kind=" << static_cast<int>(kind);
  }
}
