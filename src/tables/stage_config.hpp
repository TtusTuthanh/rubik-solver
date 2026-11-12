#pragma once
// StageConfig: extension point for Korf-style stage PDBs.
// Solvers register this; shared infra consumes it to build/load PDBs,
// move-pruning tables, and heuristic combinators.
//
// This header also owns the *nibble-packed distance-table format* shared by
// `tables::buildStagePdb` (the writer) and `core::PdbLookup` (the reader),
// so that the two can never drift apart.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/cube.hpp"
#include "core/pdb.hpp"

namespace rubik::tables {

// -- Nibble-packed distance tables -------------------------------------------
//
// The shared stage-PDB format stores one distance per entry in FOUR bits.
// The nibble value 0x0F is reserved as the "not reached" sentinel and is
// decoded as 0 at lookup time (a lower bound is always admissible), so the
// usable distance range is 0..14 -- NOT 0..15.
//
//   * A distance of 15 would be indistinguishable from the sentinel.
//   * A distance >= 16 does not fit in four bits at all: the packer would
//     have to spill bit 4 into the *neighbouring* entry, corrupting it.
//
// `kNibbleMaxDistance` is therefore the hard cap of every nibble-packed BFS.
// The byte-wide format used by `core::bfsDb` / `core::pdbLookup` is a
// DIFFERENT format: one byte per entry, sentinel 255, usable range 0..254.
//
// (Distinct names from `tables/tables_loader.hpp`'s `kNibbleSentinel`, which
// belongs to the frozen Two-Phase external-table reader.)
inline constexpr std::uint8_t kNibbleMaxDistance = 14;
inline constexpr std::uint8_t kNibbleUnreached = 0x0F;

// Pack a byte-wide distance table into the nibble format (two entries per
// byte).
//
// The input follows the `core/pdb.hpp` convention: `core::kDbSentinel` (255)
// means "not reached" and is stored as the nibble sentinel 0x0F.  Every other
// value is masked to its low four bits, and each half of the output byte is
// masked independently, so NO input value can ever spill into the
// neighbouring entry -- whatever the input is.
//
// Values above `kNibbleMaxDistance` are NOT representable by this format.
// The packer truncates them (it cannot represent them); it is the caller's
// job to reject them, which `buildStagePdb` does by refusing to build a
// pattern whose true diameter exceeds the cap.
[[nodiscard]] inline std::vector<std::uint8_t> packNibbleDistances(
    const std::vector<std::uint8_t>& dist) {
    std::vector<std::uint8_t> packed((dist.size() + 1) / 2, 0xFF);
    for (std::size_t i = 0; i < dist.size(); ++i) {
        const std::uint8_t raw = dist[i];
        const std::uint8_t v =
            (raw == core::kDbSentinel)
                ? kNibbleUnreached
                : static_cast<std::uint8_t>(raw & 0x0F);
        const std::size_t byte_idx = i >> 1;
        // Each half is masked INDEPENDENTLY, so no value -- whatever it is --
        // can ever spill into the neighbouring entry.
        if ((i & 1) == 0) {
            packed[byte_idx] = static_cast<std::uint8_t>(
                (packed[byte_idx] & 0xF0) | (v & 0x0F));
        } else {
            packed[byte_idx] = static_cast<std::uint8_t>(
                (packed[byte_idx] & 0x0F) | ((v & 0x0F) << 4));
        }
    }
    return packed;
}

// Decode one entry of a nibble-packed distance table.
//
// The sentinel 0x0F decodes to 0: an unreached state has no lower bound
// violation, because 0 <= every true distance.
[[nodiscard]] inline int unpackNibbleDistance(const std::uint8_t* packed,
                                              std::size_t index) noexcept {
    if (packed == nullptr) return 0;
    const std::uint8_t byte = packed[index >> 1];
    const std::uint8_t nibble =
        (index & 1) ? static_cast<std::uint8_t>(byte >> 4)
                    : static_cast<std::uint8_t>(byte & 0x0Fu);
    return (nibble == kNibbleUnreached) ? 0 : static_cast<int>(nibble);
}

// A single pattern database definition.
struct Pattern {
    std::string name;                   // Unique name for this PDB (e.g., "cross18", "fb")
    std::vector<int> corner_indices;    // Corner cubie indices tracked (0..7)
    std::vector<int> edge_indices;      // Edge cubie indices tracked (0..11)
    bool track_permutation = true;      // True = track pos+ori, False = ori only
    std::size_t num_states = 0;         // Total pattern state space size (for validation)

    // The set of moves that can change this pattern's tracked pieces -- its
    // "move support" M(G).
    //
    // Move indices are the *global* move indices used by
    // `StageConfig::allowed_moves` (0..17 for the 18 face moves; a solver
    // with slice moves uses its own space, consistently with its own allowed
    // move list).
    //
    // An EMPTY list means "not declared": the pattern's move support is
    // unknown, so no group containing it can be *proven* move-support
    // disjoint, and the group is therefore combined with MAX.  Declaring a
    // support is always optional -- it can only ever *enable* a sum, never
    // disable the MAX fallback.
    std::vector<int> move_support{};

    // Returns all piece indices (corners 0-7, edges 8-19) for disjointness checking.
    [[nodiscard]] std::vector<int> all_piece_indices() const {
        std::vector<int> pieces;
        pieces.reserve(corner_indices.size() + edge_indices.size());
        for (int c : corner_indices) pieces.push_back(c);           // 0..7
        for (int e : edge_indices) pieces.push_back(8 + e);         // 8..19
        return pieces;
    }
};

// A set of pattern indices that track provably disjoint piece sets.
//
// Heuristic combination:
//   * ACROSS groups: MAX (always admissible).
//   * WITHIN a group: SUM only when the group has been explicitly opted in
//     AND an automated check proves that the constituents' move-support sets
//     are disjoint over the stage's allowed move set; otherwise MAX.
//
// Piece-disjointness alone is NOT sufficient for an admissible sum: a single
// face turn advances several piece-disjoint groups at once, so h(G1) + h(G2)
// can exceed the true joint distance.  See `group_may_be_summed`.
struct DisjointGroup {
    std::vector<int> pattern_indices;   // Indices into StageConfig::patterns[]

    // Explicit opt-in: the author of this group declares that it MAY be
    // summed.  Necessary but NOT sufficient -- `group_may_be_summed` also
    // runs the piece-disjointness check and the automated move-support
    // check.  A group that is not opted in is always MAX-ed; sum-ability is
    // never inferred from piece-disjointness.
    bool sum_opt_in = false;

    // Verify that all patterns in this group track disjoint cubies.
    // Caller must ensure this holds; shared infra asserts in debug.
    [[nodiscard]] bool verify_disjoint(const std::vector<Pattern>& patterns) const {
        std::vector<bool> seen(20, false);  // 8 corners + 12 edges
        for (int pi : pattern_indices) {
            if (pi < 0 || pi >= static_cast<int>(patterns.size())) return false;
            for (int piece : patterns[pi].all_piece_indices()) {
                if (piece < 0 || piece >= 20) return false;
                if (seen[piece]) return false;  // Overlap detected
                seen[piece] = true;
            }
        }
        return true;
    }
};

// The move support of `pattern` restricted to `allowed_moves`.
//
// Returns std::nullopt when the pattern has NOT declared a move support
// (i.e. `Pattern::move_support` is empty).  A declared support that contains
// no move of the stage's alphabet is returned as an EMPTY vector: it is
// declared, and it is vacuously disjoint from every other support.
[[nodiscard]] inline std::optional<std::vector<int>> pattern_move_support(
    const Pattern& pattern, const std::vector<int>& allowed_moves) {
    if (pattern.move_support.empty()) return std::nullopt;
    std::vector<int> restricted;
    restricted.reserve(pattern.move_support.size());
    for (int m : pattern.move_support) {
        if (std::find(allowed_moves.begin(), allowed_moves.end(), m) !=
            allowed_moves.end()) {
            restricted.push_back(m);
        }
    }
    return restricted;
}

// True when the group's constituents have pairwise-disjoint move-support sets
// restricted to the stage's allowed move set.
//
// This is the sound sufficient condition for ADDING two pattern-database
// values (Korf & Felner 2002: the subgoals can be split into disjoint
// subsets "so that each operator only affects subgoals in one subset").
//
// The check MUST be computed over the stage's OWN allowed move set: over all
// 18 faces every cube cubie group shares move support with every other one
// (a face turn touches four corners and four edges), and legitimate sums --
// e.g. ZZ's left + right under the {U,R,L} alphabet -- would be rejected.
//
// Conservative by construction: a pattern that has not declared a move
// support makes the whole group fail the check.  A group with fewer than two
// constituents has nothing to compare and is vacuously disjoint.
[[nodiscard]] inline bool move_supports_disjoint(
    const DisjointGroup& group, const std::vector<Pattern>& patterns,
    const std::vector<int>& allowed_moves) {
    if (group.pattern_indices.size() < 2) return true;

    std::vector<std::vector<int>> supports;
    supports.reserve(group.pattern_indices.size());
    for (int pi : group.pattern_indices) {
        if (pi < 0 || pi >= static_cast<int>(patterns.size())) return false;
        auto support = pattern_move_support(patterns[static_cast<std::size_t>(pi)],
                                            allowed_moves);
        if (!support.has_value()) return false;  // not declared -> unprovable
        supports.push_back(std::move(*support));
    }

    for (std::size_t i = 0; i < supports.size(); ++i) {
        for (std::size_t j = i + 1; j < supports.size(); ++j) {
            for (int m : supports[i]) {
                if (std::find(supports[j].begin(), supports[j].end(), m) !=
                    supports[j].end()) {
                    return false;  // the two groups share a move
                }
            }
        }
    }
    return true;
}

// The complete admissibility gate for combining a group with SUM.
//
// A group may be summed only when ALL of the following hold:
//   1. the group is explicitly opted in (`DisjointGroup::sum_opt_in`);
//   2. the group's constituents track pairwise-disjoint pieces
//      (`DisjointGroup::verify_disjoint`);
//   3. the group's constituents have pairwise-disjoint move-support sets over
//      the stage's allowed move set (`move_supports_disjoint`).
//
// MAX is the default and the universal fallback: a group that fails any of
// the three checks is combined with MAX.
[[nodiscard]] inline bool group_may_be_summed(
    const DisjointGroup& group, const std::vector<Pattern>& patterns,
    const std::vector<int>& allowed_moves) {
    if (!group.sum_opt_in) return false;
    if (!group.verify_disjoint(patterns)) return false;
    return move_supports_disjoint(group, patterns, allowed_moves);
}

// Complete stage configuration for PDB building and heuristic wiring.
struct StageConfig {
    std::string stage_name;                         // e.g., "cross", "first_block", "eoline"
    std::vector<Pattern> patterns;                  // All PDB patterns for this stage
    std::vector<DisjointGroup> disjoint_groups;     // Proven disjoint groups (sum within, max across)
    std::vector<int> allowed_moves;                 // Global move indices (subset of 18 face moves)
    std::function<bool(const CubeState&)> goal_predicate;  // Stage goal test
    std::vector<CubeState> goal_states;             // Goal state(s) for backward BFS (multi-source)

    // Validate configuration (debug builds).
    [[nodiscard]] bool validate() const {
        if (stage_name.empty()) return false;
        if (patterns.empty()) return false;
        if (allowed_moves.empty()) return false;
        if (!goal_predicate) return false;
        if (goal_states.empty()) return false;
        for (const auto& g : disjoint_groups) {
            if (!g.verify_disjoint(patterns)) return false;
        }
        // Every pattern must be in exactly one disjoint group or be a singleton max-group.
        // (Handled by heuristic combinator; here we just check group validity.)
        return true;
    }

    // True when the group at `index` may be combined with SUM, i.e. when the
    // full admissibility gate passes (see `group_may_be_summed`).
    [[nodiscard]] bool group_may_be_summed(std::size_t index) const {
        if (index >= disjoint_groups.size()) return false;
        return ::rubik::tables::group_may_be_summed(disjoint_groups[index],
                                                    patterns, allowed_moves);
    }
};

// Helper: global move index for the 18 face moves in standard order.
// Order: U, U', U2, R, R', R2, F, F', F2, D, D', D2, L, L', L2, B, B', B2
inline std::vector<int> all_18_face_move_indices() {
    return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
}

// Helper: move indices without D moves (for OLL: 15 moves).
inline std::vector<int> all_18_except_D() {
    return {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17};  // Skip 9,10,11 (D/D'/D2)
}

// Helper: R/L/U only (9 moves for ZZ F2L/COLL/EPLL).
inline std::vector<int> rlu_moves() {
    // U(0,1,2), R(3,4,5), L(12,13,14)
    return {0, 1, 2, 3, 4, 5, 12, 13, 14};
}

// Helper: M/U only (6 moves for Roux LSE).
inline std::vector<int> mu_moves() {
    // M is not in the 18 face moves; this is for reference only.
    // Actual LSE uses slice moves; handled separately in solver.
    return {};
}

}  // namespace rubik::tables
