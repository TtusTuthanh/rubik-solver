#include "tables/move_pruning.hpp"

#include <algorithm>

namespace rubik::tables {

MovePruningTable::MovePruningTable(const std::vector<int>& allowed_moves)
    : allowed_moves_(allowed_moves) {
    // Initialize all to false (no pruning by default).
    for (int i = 0; i < 18; ++i) {
        for (int j = 0; j < 18; ++j) {
            redundant_[i][j] = false;
        }
    }

    // Rule 1: Same face is always redundant (composition would be a single move or identity).
    // For each allowed move, mark all other allowed moves on the same face as redundant.
    for (int prev : allowed_moves_) {
        int prev_face = kMoveFace[prev];
        for (int curr : allowed_moves_) {
            if (kMoveFace[curr] == prev_face) {
                redundant_[prev][curr] = true;
            }
        }
    }

    // Rule 2: Opposite faces — only allow canonical order (lower face index first).
    // If prev_face > curr_face and they are opposites, curr is redundant after prev.
    for (int prev : allowed_moves_) {
        int prev_face = kMoveFace[prev];
        int opp_face = kOppositeFace[prev_face];
        for (int curr : allowed_moves_) {
            if (kMoveFace[curr] == opp_face) {
                int curr_face = kMoveFace[curr];
                if (prev_face > curr_face) {  // Only allow lower face index first
                    redundant_[prev][curr] = true;
                }
            }
        }
    }

    // A move is never redundant after itself? Actually, same-face rule already covers this
    // (a move followed by the same move or its inverse or its double is redundant).
    // But we also need to handle the case where prev=-1 (start of search).
    // The searcher handles prev=-1 specially (no pruning at root).
}

}  // namespace rubik::tables