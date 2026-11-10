#pragma once
// StageConfig: extension point for Korf-style stage PDBs.
// Solvers register this; shared infra consumes it to build/load PDBs,
// move-pruning tables, and heuristic combinators.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/cube.hpp"

namespace rubik::tables {

// A single pattern database definition.
struct Pattern {
    std::string name;                   // Unique name for this PDB (e.g., "cross18", "fb")
    std::vector<int> corner_indices;    // Corner cubie indices tracked (0..7)
    std::vector<int> edge_indices;      // Edge cubie indices tracked (0..11)
    bool track_permutation = true;      // True = track pos+ori, False = ori only
    std::size_t num_states = 0;         // Total pattern state space size (for validation)
    
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
// Heuristic combination: SUM within a group, MAX across groups.
struct DisjointGroup {
    std::vector<int> pattern_indices;   // Indices into StageConfig::patterns[]
    
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
};

// Helper: global move index for the 18 face moves in standard order.
// Order: U, U', U2, R, R', R2, F, F', F2, D, D', D2, L, L', L2, B, B', B2
inline std::vector<int> all_18_face_move_indices() {
    return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
}

// Helper: move indices without D moves (for OLL: 15 moves).
inline std::vector<int> all_18_except_D() {
    return {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 13, 14, 15, 16, 17};  // Skip 9,10,11 (D/D'/D2)
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