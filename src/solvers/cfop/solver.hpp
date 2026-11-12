#pragma once
// CFOP solver: Cross -> F2L (4 pairs) -> OLL -> PLL.
// See docs/SPEC.md Section 7. Per-stage IDA* with small pattern-database
// heuristics derived from the frozen core cubie model. Fingertricks are not
// required; solutions are move-optimal per stage (subject to the restricted
// move set) but not fingertrick-optimal.
#include <expected>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"

namespace rubik::cfop {

// Stage names in order. F2L is emitted as four separate stages
// (f2l_1..f2l_4), one per pair, so the total stage list is:
// cross, f2l_1, f2l_2, f2l_3, f2l_4, oll, pll.
std::vector<std::string> stage_names();

// Restricted move sets per stage. Tokens are in rubik::all_face_moves()
// order where applicable. Cross/F2L/PLL allow all 18 face moves; OLL
// forbids D moves (15 tokens) so the first two layers stay intact in the
// ordinary CFOP sense (any OLL algorithm needs no D move). PLL allows D
// because several standard PLL algorithms use D moves.
std::vector<std::string> cross_allowed_moves();
std::vector<std::string> f2l_allowed_moves();
std::vector<std::string> oll_allowed_moves();
std::vector<std::string> pll_allowed_moves();

// Standard PLL algorithms (21, Speedsolving Wiki HTM/speedsolving face-only
// versions, each token valid in pll_allowed_moves()). solve() tries these
// with U pre/post rotations first (simulation-verified in tests); per-stage
// IDA* remains as fallback. Each string is whitespace-separated tokens.
std::vector<std::string> pll_algorithms();

// Allowed tokens for a stage name (see stage_names()).
std::vector<std::string> allowed_moves_for_stage(const std::string& stage_name);

// Goal predicates. Each predicate is checked by solve() after its stage and
// can be checked externally by tests on the cubie state.
// Cubie order (src/core/cube.hpp): corners 0..7 = URF,UFL,ULB,UBR,DFR,DLF,DBL,DRB;
// edges 0..11 = UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR.
bool is_cross_solved(const CubeState& state);
// pairIndex 0..3 = FR(DFR+FR), FL(DLF+FL), BL(DBL+BL), BR(DRB+BR).
bool is_f2l_pair_solved(const CubeState& state, int pairIndex);
bool is_f2l_solved(const CubeState& state);
bool is_oll_solved(const CubeState& state);
bool is_pll_solved(const CubeState& state);

// CFOP solver. Returns a Solution with stages in stage_names() order whose
// concatenated moves solve the cube. Respects SolveOptions:
//   - timeout_ms > 0 bounds the whole solve; on expiry returns Timeout.
//   - stop_token: returns Cancelled when requested.
//   - max_depth is a per-stage bound (each stage's IDA* exceeds it -> Timeout).
// Invalid/unreachable cubie states return Unsolvable.
std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options);

// ---- Test accessors (admissibility testing). ----
// Not part of the frozen solve() interface; used only by tests/cfop to
// verify that the per-stage heuristic never overestimates the true stage
// distance (admissibility). Mirrors the StageKind used internally by runIda().
enum class TestStageKind { Cross, F2L_1, Oll, Pll };
int testStageHeuristic(TestStageKind kind, int pairIndex,
                       const CubeState& state);

}  // namespace rubik::cfop
