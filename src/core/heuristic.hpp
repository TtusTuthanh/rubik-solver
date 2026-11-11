#pragma once
// Heuristic combinator for Korf-style stage PDBs.
//
// Combination rules (the structural admissibility gate, see
// `tables::group_may_be_summed`):
//   * ACROSS groups: MAX (always admissible).
//   * WITHIN a group: SUM only when the group is EXPLICITLY opted in AND an
//     automated check proves that the constituents' move-support sets are
//     disjoint over the stage's allowed move set; otherwise MAX.
//
// Piece-disjointness alone is NOT sufficient for an admissible SUM: one face
// turn advances several piece-disjoint groups at once, so h(G1) + h(G2) can
// exceed the true joint distance (Korf & Felner 2002: the subgoals can be
// added only when "each operator only affects subgoals in one subset").
// MAX is the default and the universal fallback.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "tables/stage_config.hpp"

namespace rubik::core {

// A single PDB lookup callable.
// Wraps a pattern database (nibble-packed byte array) and its index function.
struct PdbLookup {
    std::string name;                          // PDB name (for debugging)
    const std::uint8_t* data = nullptr;        // Nibble-packed distance table (or nullptr = h=0)
    std::size_t num_entries = 0;               // Total entries in the table
    std::function<std::uint32_t(const CubeState&)> index_fn;  // State -> pattern index

    // Lookup heuristic value for a state. Returns 0 if data is null or index OOB.
    [[nodiscard]] int operator()(const CubeState& state) const noexcept {
        if (!data || !index_fn) return 0;
        const std::uint32_t idx = index_fn(state);
        if (idx >= num_entries) return 0;
        // Nibble-packed: byte idx/2, low nibble even, high nibble odd.
        // The "unreached" sentinel 0x0F decodes to 0 (a lower bound is always
        // admissible). The decode lives in `tables/stage_config.hpp` so that
        // the writer (`buildStagePdb`) and this reader cannot drift apart.
        return tables::unpackNibbleDistance(data, idx);
    }

    [[nodiscard]] bool valid() const noexcept { return data != nullptr && index_fn != nullptr; }
};

// Combined heuristic for a stage.
//
// The structural admissibility gate is evaluated ONCE, in the constructor:
// a group is combined with SUM only when it is explicitly opted in, its
// constituents track disjoint pieces, and its constituents' move-support sets
// are disjoint over the stage's allowed move set.  Everything else is MAX.
class StageHeuristic {
public:
    StageHeuristic() = default;

    // Without patterns and without a stage move set the gate cannot be
    // evaluated, so every group is MAX-ed.  This is the safe default: MAX is
    // always admissible.
    StageHeuristic(const std::vector<PdbLookup>& pdbs,
                   const std::vector<tables::DisjointGroup>& groups)
        : StageHeuristic(pdbs, groups, {}, {}) {}

    StageHeuristic(const std::vector<PdbLookup>& pdbs,
                   const std::vector<tables::DisjointGroup>& groups,
                   const std::vector<tables::Pattern>& patterns,
                   const std::vector<int>& allowed_moves)
        : pdbs_(pdbs), groups_(groups) {
        group_sums_.reserve(groups_.size());
        for (const auto& g : groups_) {
            group_sums_.push_back(
                tables::group_may_be_summed(g, patterns, allowed_moves));
        }
    }

    // Evaluate combined heuristic for a state.
    [[nodiscard]] int operator()(const CubeState& state) const noexcept {
        if (pdbs_.empty()) return 0;

        // If no disjoint groups defined, fall back to MAX of all PDBs (always admissible).
        if (groups_.empty()) {
            int h = 0;
            for (const auto& pdb : pdbs_) {
                h = std::max(h, pdb(state));
            }
            return h;
        }

        // Combine within each group (SUM only when the structural gate
        // passed, MAX otherwise), then MAX across groups.
        int best = 0;
        for (std::size_t gi = 0; gi < groups_.size(); ++gi) {
            const auto& group = groups_[gi];
            const bool sum = gi < group_sums_.size() && group_sums_[gi];
            int group_value = 0;
            for (int pi : group.pattern_indices) {
                if (pi < 0 || pi >= static_cast<int>(pdbs_.size())) continue;
                const int value = pdbs_[static_cast<std::size_t>(pi)](state);
                if (sum) {
                    group_value += value;
                } else {
                    group_value = std::max(group_value, value);
                }
            }
            best = std::max(best, group_value);
        }
        return best;
    }

    // Check if all PDBs are loaded and valid.
    [[nodiscard]] bool allValid() const noexcept {
        for (const auto& pdb : pdbs_) {
            if (!pdb.valid()) return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t numPdbs() const noexcept { return pdbs_.size(); }
    [[nodiscard]] std::size_t numGroups() const noexcept { return groups_.size(); }

    // True when the group at `index` is combined with SUM (i.e. the
    // structural admissibility gate passed).  Diagnostics and tests.
    [[nodiscard]] bool groupIsSummed(std::size_t index) const noexcept {
        return index < group_sums_.size() && group_sums_[index];
    }

private:
    std::vector<PdbLookup> pdbs_;
    std::vector<tables::DisjointGroup> groups_;
    std::vector<bool> group_sums_;  // one flag per group: SUM allowed?
};

// Factory: create a StageHeuristic from a StageConfig and loaded PDB data.
inline StageHeuristic makeStageHeuristic(
    const tables::StageConfig& config,
    const std::vector<const std::uint8_t*>& pdb_data_ptrs,  // Same order as config.patterns
    const std::vector<std::function<std::uint32_t(const CubeState&)>>& index_fns) {
    std::vector<PdbLookup> pdbs;
    pdbs.reserve(config.patterns.size());
    for (std::size_t i = 0; i < config.patterns.size(); ++i) {
        pdbs.push_back(PdbLookup{
            .name = config.patterns[i].name,
            .data = (i < pdb_data_ptrs.size()) ? pdb_data_ptrs[i] : nullptr,
            .num_entries = config.patterns[i].num_states,
            .index_fn = (i < index_fns.size()) ? index_fns[i] : nullptr
        });
    }
    // The stage's patterns and allowed move set are handed to the combinator
    // so that the structural admissibility gate can be evaluated.
    return StageHeuristic(pdbs, config.disjoint_groups, config.patterns,
                           config.allowed_moves);
}

}  // namespace rubik::core
