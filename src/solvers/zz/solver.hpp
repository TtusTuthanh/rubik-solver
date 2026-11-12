#pragma once
// ZZ solver: EOLine -> ZZF2L (left and right blocks, only R/L/U) ->
// COLL -> EPLL. See docs/SPEC.md Section 7. Per-stage IDA* with
// pattern-database heuristics derived from the frozen core cubie model.
// Fingertricks are not required; solutions are move-optimal per stage
// (subject to the restricted move set) but not fingertrick-optimal.
//
// Cubie order (src/core/cube.hpp): corners 0..7 = URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB;
// edges 0..11 = UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR.
//
// Edge orientation (eo) is the core's U/D orientation: R, L and U moves
// never flip an edge (verified against rubik::apply), so any sequence of
// R/L/U moves preserves an oriented edge set and fixes the DF/DB edge
// positions pointwise. That is the ZZ key property used below.
//
// Stage definitions (D is the down layer):
//   eoline      ("EOLine"): all edges oriented (eo == 0 everywhere) and
//               the DF (5) and DB (7) edges solved in place, forming the
//               line on the D face. R/L/U moves preserve this stage.
//   zzf2l_left  ("left block"): the L 1x2x3 block (x=-1, y=-1..0): corners
//               DLF(5),DBL(6); edges DL(6),FL(9),BL(10). Solved with
//               R/L/U only (R moves are setup moves for cubies that start
//               on the R side, which L/U alone cannot reach).
//   zzf2l_right ("right block"): the R 1x2x3 block (x=+1, y=-1..0): corners
//               DFR(4),DRB(7); edges DR(4),FR(8),BR(11), preserving the
//               left block. Together with EOLine (DF,DB) this solves the
//               first two layers from D: all D corners, all D edges and
//               all middle-layer edges FR,FL,BL,BR.
//   coll        ("COLL"): the four U corners solved in permutation and
//               orientation (cp/co of positions 0..3 identity), preserving
//               ZZF2L. (Stronger than orientation-only: a solved corner
//               set is oriented, so this implies the usual COLL
//               orientation predicate.)
//   eppl        ("EPLL"): solve the cube, i.e. permute the four U edges
//               (0..3) into place preserving COLL and ZZF2L.
#include <expected>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"

namespace rubik::zz {

// Stage names in order.
std::vector<std::string> stage_names();

// Restricted move sets per stage. EOLine allows all 18 face moves;
// ZZF2L / COLL / EPLL allow exactly the 9 tokens on the R, L and U faces.
std::vector<std::string> eoline_allowed_moves();
std::vector<std::string> zzf2l_allowed_moves();
std::vector<std::string> zzf2l_left_allowed_moves();
std::vector<std::string> zzf2l_right_allowed_moves();
std::vector<std::string> coll_allowed_moves();
std::vector<std::string> eppl_allowed_moves();

// Allowed tokens for a stage name (see stage_names()).
std::vector<std::string> allowed_moves_for_stage(const std::string& stage_name);

// Goal predicates. Each predicate is checked by solve() after its stage and
// can be checked externally by tests on the cubie state. The *_block /
// is_f2l_* / is_epll_* / is_eo_line_* spellings are aliases for convenience.
bool is_eoline_solved(const CubeState& state);
bool is_eo_line_solved(const CubeState& state);
bool is_eol_solved(const CubeState& state);
bool is_zzf2l_left_solved(const CubeState& state);
bool is_f2l_left_solved(const CubeState& state);
bool is_left_block_solved(const CubeState& state);
bool is_zzf2l_right_solved(const CubeState& state);
bool is_f2l_right_solved(const CubeState& state);
bool is_right_block_solved(const CubeState& state);
bool is_zzf2l_solved(const CubeState& state);
bool is_f2l_solved(const CubeState& state);
bool is_coll_solved(const CubeState& state);
bool is_eppl_solved(const CubeState& state);
bool is_epll_solved(const CubeState& state);
bool is_last_layer_solved(const CubeState& state);

// ZZ solver. Returns a Solution with stages in stage_names() order whose
// concatenated moves solve the cube. Respects SolveOptions:
//   - timeout_ms > 0 bounds the whole solve; on expiry returns Timeout.
//   - stop_token: returns Cancelled when requested.
//   - max_depth is a per-stage bound (each stage's IDA* exceeds it -> Timeout).
// Invalid/unreachable cubie states return Unsolvable.
std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options);

// ---- test accessor (exposed for admissibility tests only) ----
// Returns the per-stage PDB heuristic for the named stage without exposing
// the Pdbs struct or the StageKind enum (both live in the anonymous namespace
// of solver.cpp). stage_name is one of stage_names(); unknown names map to
// the EPLL stage.
int stageHeuristicForTest(const std::string& stage_name, const CubeState& s);

}  // namespace rubik::zz
