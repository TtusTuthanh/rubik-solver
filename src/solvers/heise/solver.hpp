#pragma once
// Heise solver: squares -> F2L-1 -> corner-edge pairs -> final corners.
// See docs/methods/heise.md (canonical 4 stages) and docs/SPEC.md Section 7.
// Per-stage IDA* with pattern-database heuristics derived from the frozen
// core cubie model. Fingertricks are not required; solutions are move-optimal
// per stage (subject to the stage move set) but not fingertrick-optimal.
//
// Cubie order (src/core/cube.hpp): corners 0..7 = URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB;
// edges 0..11 = UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR.
//
// Stage definitions (D is the down layer; squares are the four D 1x2x2 blocks,
// the DR (BR-side) square is the designated incomplete one, per the wiki note
// that "one of these blocks should be just two centers and an edge"):
//   squares            ("squares"): the four D 1x2x2 blocks, i.e. the D edges
//     DR(4),DF(5),DL(6),DB(7) solved in place and oriented and the four D
//     corner positions oriented (co[4..7] == 0) with D-corner permutation
//     still free. One color appears in no block: the F/B color orthogonal
//     to the D-layer squares is fixed by the in-place D requirement.
//   f2l_minus_1        ("F2L-1"): pair the squares + orient last-layer edges.
//     Squares + the middle edges FR(8),FL(9),BL(10) solved, with the
//     last-layer (U) edges oriented (eo[0..3] == 0). The one remaining F2L
//     pair is DRB(7)+BR(11).
//   corner_edge_pairs  ("pairs"): create two corner-edge pairs, solve all
//     edges + two corners. F2L-1 + the remaining 5 edges (BR(11) and the
//     four U edges 0..3) solved + 2 corners (DRB(7) and URF(0)) solved.
//     Only the 3 corners UFL,ULB,UBR remain unsolved.
//   final_corners      ("final"): solve the final 3 corners with a commutator
//     (any face-move commutator solution), i.e. solve the cube.
#include <expected>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"

namespace rubik::heise {

// Stage names in order.
std::vector<std::string> stage_names();

// Restricted move sets per stage. The canonical Heise method uses all face
// moves at every step (fingertricks are not required), so every stage allows
// exactly the 18 face moves.
std::vector<std::string> squares_allowed_moves();
std::vector<std::string> f2l_allowed_moves();
std::vector<std::string> corner_edge_pairs_allowed_moves();
std::vector<std::string> final_corners_allowed_moves();

// Allowed tokens for a stage name (see stage_names()).
std::vector<std::string> allowed_moves_for_stage(const std::string& stage_name);

// Goal predicates. Each predicate is checked by solve() after its stage and
// can be checked externally by tests on the cubie state. The aliases below
// use alternative historical spellings.
bool is_squares_solved(const CubeState& state);
bool is_square_solved(const CubeState& state);
bool is_heise_blocks_solved(const CubeState& state);
bool is_f2l_minus_1_solved(const CubeState& state);
bool is_f2l1_solved(const CubeState& state);
bool is_f2l_1_solved(const CubeState& state);
bool is_corner_edge_pairs_solved(const CubeState& state);
bool is_pairs_solved(const CubeState& state);
bool is_last_edges_solved(const CubeState& state);
bool is_final_corners_solved(const CubeState& state);

// Heise solver. Returns a Solution with stages in stage_names() order whose
// concatenated moves solve the cube. Respects SolveOptions:
//   - timeout_ms > 0 bounds the whole solve; on expiry returns Timeout.
//   - stop_token: returns Cancelled when requested.
//   - max_depth is a per-stage bound (each stage's IDA* exceeds it -> Timeout).
// Invalid/unreachable cubie states return Unsolvable.
std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options);

}  // namespace rubik::heise
