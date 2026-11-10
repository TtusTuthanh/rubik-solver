#pragma once
// Move-pruning table for redundant/opposite move elimination per stage move set.
// Built once per stage at solver init; used in IDA* to prune dead branches.

#include <array>
#include <cstdint>
#include <vector>

#include "core/moves.hpp"
#include "tables/stage_config.hpp"

namespace rubik::tables {

// Face indices in standard order (matching all_face_moves() order).
// U=0, R=1, F=2, D=3, L=4, B=5
inline constexpr std::array<int, 18> kMoveFace = {
    0, 0, 0,   // U, U', U2
    1, 1, 1,   // R, R', R2
    2, 2, 2,   // F, F', F2
    3, 3, 3,   // D, D', D2
    4, 4, 4,   // L, L', L2
    5, 5, 5    // B, B', B2
};

// Opposite face pairs: (U,D), (F,B), (L,R)
inline constexpr std::array<int, 6> kOppositeFace = {3, 4, 5, 0, 1, 2};  // U<->D, R<->L, F<->B

// Move-pruning table for a specific stage move set.
// Encodes: same-face continuations are dead; opposite faces only in canonical order (lower index first).
class MovePruningTable {
public:
    // Build from a list of allowed global move indices (subset of 0..17).
    explicit MovePruningTable(const std::vector<int>& allowed_moves);

    // Default: no pruning (allows all moves).
    MovePruningTable() = default;

    // Returns true if `curr` is redundant after `prev` (should be pruned).
    // Both `prev` and `curr` are global move indices (0..17).
    [[nodiscard]] bool isRedundant(int prev, int curr) const noexcept {
        if (prev < 0 || prev >= 18 || curr < 0 || curr >= 18) return false;
        return redundant_[prev][curr];
    }

    // Returns the list of allowed moves for this stage.
    [[nodiscard]] const std::vector<int>& allowedMoves() const noexcept { return allowed_moves_; }

    // Returns a filtered move list for a given previous move (excludes redundant).
    [[nodiscard]] std::vector<int> filterMoves(int prev) const noexcept {
        std::vector<int> filtered;
        filtered.reserve(allowed_moves_.size());
        for (int m : allowed_moves_) {
            if (!isRedundant(prev, m)) filtered.push_back(m);
        }
        return filtered;
    }

private:
    std::vector<int> allowed_moves_;
    // redundant_[prev][curr] = true if curr is redundant after prev.
    std::array<std::array<bool, 18>, 18> redundant_{};
};

// Build a move-pruning table for the standard 18 face moves.
inline MovePruningTable makeFullMovePruningTable() {
    return MovePruningTable(rubik::tables::all_18_face_move_indices());
}

}  // namespace rubik::tables