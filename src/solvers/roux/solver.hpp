#pragma once
// Roux solver: First block (1x2x3) -> Second block (1x2x3) -> CMLL -> LSE.
// See docs/SPEC.md Section 7. Per-stage IDA* with pattern-database
// heuristics derived from the frozen core cubie model. Fingertricks are not
// required; solutions are move-optimal per stage (subject to the restricted
// move set) but not fingertrick-optimal.
//
// Cubie order (src/core/cube.hpp): corners 0..7 = URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB;
// edges 0..11 = UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR.
//
// Stage definitions (block faces are L and R, D is the down layer):
//   first_block  ("FB"): the L 1x2x3 block (x=-1, y=-1..0, z=-1..1, includes
//     the L centre): corners DLF(5),DBL(6); edges DL(6),FL(9),BL(10).
//   second_block ("SB"): the R 1x2x3 block (x=+1, y=-1..0, z=-1..1, includes
//     the R centre): corners DFR(4),DRB(7); edges DR(4),FR(8),BR(11).
//     Together FB+SB solve all 4 D corners and the 6 edges
//     DR,DL,FR,FL,BL,BR -- exactly the edges fixed pointwise by M and U.
//   cmll: solve the 4 U corners (URF,UFL,ULB,UBR) in permutation and
//     orientation, preserving FB+SB, up to a whole U rotation. A U rotation
//     is left for LSE, whose U moves can finish it; M moves do not affect
//     corners at all, so corner orientation must be fully solved here.
//     Is_cmll_solved therefore accepts exactly the 4 U rotations of the
//     solved U corners (with zero twist). For any valid cube state with
//     FB+SB solved this condition is also sufficient for LSE solvability:
//     the M/U edge group acts as the full symmetric group on the remaining
//     6 edges with all even-orientation patterns reachable (verified by
//     exhaustive BFS over the M/U group in development).
//   lse: solve the last 6 edges UR,UF,UL,UB,DF,DB with M and U moves only,
//     finishing the U-corner rotation and returning the U/F/D/B centres
//     to identity, i.e. solve the cube.
#include <expected>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"

namespace rubik::roux {

// Stage names in order.
std::vector<std::string> stage_names();

// Restricted move sets per stage. FB/SB/CMLL allow all 18 face moves;
// LSE allows exactly the 6 tokens M, M', M2, U, U', U2.
std::vector<std::string> first_block_allowed_moves();
std::vector<std::string> second_block_allowed_moves();
std::vector<std::string> cmll_allowed_moves();
std::vector<std::string> lse_allowed_moves();

// Allowed tokens for a stage name (see stage_names()).
std::vector<std::string> allowed_moves_for_stage(const std::string& stage_name);

// Goal predicates. Each predicate is checked by solve() after its stage and
// can be checked externally by tests on the cubie state.
bool is_first_block_solved(const CubeState& state);
bool is_second_block_solved(const CubeState& state);
bool is_cmll_solved(const CubeState& state);
bool is_lse_solved(const CubeState& state);

// Roux solver. Returns a Solution with stages in stage_names() order whose
// concatenated moves solve the cube. Respects SolveOptions:
//   - timeout_ms > 0 bounds the whole solve; on expiry returns Timeout.
//   - stop_token: returns Cancelled when requested.
//   - max_depth is a per-stage bound (each stage's IDA* exceeds it -> Timeout).
// Invalid/unreachable cubie states return Unsolvable.
std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options);

}  // namespace rubik::roux
