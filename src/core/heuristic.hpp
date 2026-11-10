#pragma once
// Heuristic combinator for Korf-style stage PDBs.
// Implements: SUM within disjoint groups, MAX across groups.

#include <cstdint>
#include <functional>
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
        const std::uint8_t byte = data[idx >> 1];
        const std::uint8_t nibble = (idx & 1) ? (byte >> 4) : (byte & 0x0F);
        return (nibble == 0x0F) ? 0 : static_cast<int>(nibble);  // Sentinel 0xF -> 0
    }

    [[nodiscard]] bool valid() const noexcept { return data != nullptr && index_fn != nullptr; }
};

// Combined heuristic for a stage using disjoint-group combinator.
// Within each DisjointGroup: SUM of constituent PDBs (admissible because pieces don't interact).
// Across DisjointGroups: MAX (admissible because groups may overlap).
class StageHeuristic {
public:
    StageHeuristic() = default;

    StageHeuristic(const std::vector<PdbLookup>& pdbs,
                   const std::vector<tables::DisjointGroup>& groups)
        : pdbs_(pdbs), groups_(groups) {}

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

        // Compute SUM within each group, then MAX across groups.
        int max_group_sum = 0;
        for (const auto& group : groups_) {
            int group_sum = 0;
            for (int pi : group.pattern_indices) {
                if (pi >= 0 && pi < static_cast<int>(pdbs_.size())) {
                    group_sum += pdbs_[pi](state);
                }
            }
            max_group_sum = std::max(max_group_sum, group_sum);
        }
        return max_group_sum;
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

private:
    std::vector<PdbLookup> pdbs_;
    std::vector<tables::DisjointGroup> groups_;
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
    return StageHeuristic(pdbs, config.disjoint_groups);
}

}  // namespace rubik::core