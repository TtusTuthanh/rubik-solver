#pragma once
// Move-pruning table for redundant/opposite move elimination per stage move set.
//
// Korf (1997) uses two lossless rules for the face-turn metric:
//   * two consecutive turns of the same face compose to a single turn (or to
//     the identity), so no optimal solution contains them;
//   * opposite faces commute, so only one of the two consecutive orders need
//     be kept.
// Together they take the asymptotic branching factor from 18 to about
// 13.34847.
//
// The table is header-only on purpose: the solver libraries link `rubik_core`
// only, and must be able to build a pruning table without gaining a link
// dependency on `rubik_tables`.

#include <array>
#include <cstdint>
#include <vector>

#include "core/searcher.hpp"
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

// Opposite face pairs: (U,D), (R,L), (F,B)
inline constexpr std::array<int, 6> kOppositeFace = {3, 4, 5, 0, 1, 2};  // U<->D, R<->L, F<->B

// Move-pruning table for a specific stage move set.
//
// Encodes: same-face continuations are dead; opposite faces only in canonical
// order (lower face index first).
//
// The table can describe any move-index space. Moves are partitioned into
// *groups*: two moves in the same group are redundant in sequence. A group
// may declare an *opposite* partner; for opposite groups only one of the two
// consecutive orders is kept, because opposite faces commute. A group with no
// opposite partner (for example the `M` slice in Roux's LSE stage) takes part
// only in the same-group rule.
class MovePruningTable {
 public:
  // A group of moves. `opposite` is the group id of the opposite face, or -1
  // when the group has no opposite partner.
  struct Group {
    int id = 0;
    int opposite = -1;
  };

  // Default: no pruning (allows all moves).
  MovePruningTable() = default;

  // The standard 18 face moves, grouped by face with the three opposite-face
  // pairs wired up.
  explicit MovePruningTable(const std::vector<int>& allowed_moves);

  // General constructor over an explicit move-index space.
  //
  // `move_group` maps every move index to its group id; its size is the
  // number of moves in the space. `groups` declares the opposite partner of
  // each group.
  MovePruningTable(const std::vector<int>& allowed_moves,
                   const std::vector<int>& move_group,
                   const std::vector<Group>& groups);

  // Returns true if `curr` is redundant after `prev` (should be pruned).
  // Both `prev` and `curr` are move indices in this table's move space.
  // Out-of-range indices are never redundant.
  [[nodiscard]] bool isRedundant(int prev, int curr) const noexcept {
    if (prev < 0 || prev >= num_moves_ || curr < 0 || curr >= num_moves_) {
      return false;
    }
    const std::size_t base =
        static_cast<std::size_t>(prev) * static_cast<std::size_t>(num_moves_);
    return redundant_[base + static_cast<std::size_t>(curr)] != 0;
  }

  // Returns the list of allowed moves for this stage.
  [[nodiscard]] const std::vector<int>& allowedMoves() const noexcept {
    return allowed_moves_;
  }

  // Number of moves in this table's move space.
  [[nodiscard]] int numMoves() const noexcept { return num_moves_; }

  // Returns a filtered move list for a given previous move (excludes redundant).
  [[nodiscard]] std::vector<int> filterMoves(int prev) const noexcept {
    std::vector<int> filtered;
    filtered.reserve(allowed_moves_.size());
    for (int m : allowed_moves_) {
      if (!isRedundant(prev, m)) filtered.push_back(m);
    }
    return filtered;
  }

  // View of this table as a searcher pruning policy. The returned policy
  // points into this table, so the table must outlive the policy.
  [[nodiscard]] rubik::core::MovePruningPolicy policy() const noexcept {
    return rubik::core::MovePruningPolicy(
        redundant_.empty() ? nullptr : redundant_.data(), num_moves_);
  }

 private:
  // Marks (prev, curr) redundant under the two Korf rules.
  void build(const std::vector<int>& move_group,
             const std::vector<int>& group_opposite);

  std::vector<int> allowed_moves_;
  std::vector<std::uint8_t> redundant_;  // num_moves_ x num_moves_, row-major
  int num_moves_ = 0;
};

// Build a move-pruning table for the standard 18 face moves.
inline MovePruningTable makeFullMovePruningTable() {
  return MovePruningTable(rubik::tables::all_18_face_move_indices());
}

inline MovePruningTable::MovePruningTable(
    const std::vector<int>& allowed_moves)
    : allowed_moves_(allowed_moves) {
  // The 18 face moves, grouped by face.
  std::vector<int> move_group(18);
  for (int i = 0; i < 18; ++i) {
    move_group[static_cast<std::size_t>(i)] = kMoveFace[static_cast<std::size_t>(i)];
  }
  // Opposite-face partners, indexed by face.
  std::vector<int> group_opposite(6);
  for (int f = 0; f < 6; ++f) {
    group_opposite[static_cast<std::size_t>(f)] =
        kOppositeFace[static_cast<std::size_t>(f)];
  }
  build(move_group, group_opposite);
}

inline MovePruningTable::MovePruningTable(
    const std::vector<int>& allowed_moves,
    const std::vector<int>& move_group,
    const std::vector<Group>& groups)
    : allowed_moves_(allowed_moves) {
  int max_group = -1;
  for (const auto& g : groups) {
    if (g.id > max_group) max_group = g.id;
  }
  std::vector<int> group_opposite(static_cast<std::size_t>(max_group + 1), -1);
  for (const auto& g : groups) {
    group_opposite[static_cast<std::size_t>(g.id)] = g.opposite;
  }
  build(move_group, group_opposite);
}

inline void MovePruningTable::build(const std::vector<int>& move_group,
                                     const std::vector<int>& group_opposite) {
  num_moves_ = static_cast<int>(move_group.size());
  redundant_.assign(
      static_cast<std::size_t>(num_moves_) * static_cast<std::size_t>(num_moves_),
      0);

  auto groupOf = [&](int move) -> int {
    if (move < 0 || move >= num_moves_) return -1;
    return move_group[static_cast<std::size_t>(move)];
  };
  auto oppositeOf = [&](int group) -> int {
    if (group < 0 || group >= static_cast<int>(group_opposite.size())) return -1;
    return group_opposite[static_cast<std::size_t>(group)];
  };

  // Rule 1: same group is always redundant (the composition would be a single
  // move or the identity).
  // Rule 2: opposite groups only in canonical order (lower group id first).
  //
  // Only pairs whose two moves are both in the allowed set are marked; a move
  // outside the stage's move set is never considered.
  for (int prev : allowed_moves_) {
    const int prev_group = groupOf(prev);
    if (prev_group < 0) continue;
    for (int curr : allowed_moves_) {
      const int curr_group = groupOf(curr);
      if (curr_group < 0) continue;
      bool redundant = false;
      if (curr_group == prev_group) {
        redundant = true;
      } else if (oppositeOf(prev_group) == curr_group && prev_group > curr_group) {
        redundant = true;
      }
      if (redundant) {
        const std::size_t base =
            static_cast<std::size_t>(prev) * static_cast<std::size_t>(num_moves_);
        redundant_[base + static_cast<std::size_t>(curr)] = 1;
      }
    }
  }
}

}  // namespace rubik::tables
