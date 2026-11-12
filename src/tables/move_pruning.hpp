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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
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

// Upper bound on a group id accepted by the general constructor.
//
// Group ids name faces or slices -- a handful of them.  The bound keeps a
// bad id from sizing a huge allocation; an id outside [0, kMaxGroupId] is
// REJECTED (see the general constructor).
inline constexpr int kMaxGroupId = 1024;

// Move-pruning table for a specific stage move set.
//
// Encodes: same-face continuations are dead; opposite faces only in canonical
// order (lower face index first).
//
// CANONICAL DIRECTION (ratified 2026-11-11, finding F-01): for an opposite
// pair the LOWER group id goes first -- e.g. for {U(0), D(3)} the order
// U -> D is kept and D -> U is rejected.  This is deliberately NOT flipped:
// the pre-`9c87376` table used `prev_face > curr_face` (the same rule), the
// committed test `tests/tables/test_move_pruning.cpp:68-69` pins it, and
// `shared/spec.md` requires the 18-face behaviour to be preserved
// byte-for-byte.  Heise's own implementation keeps the HIGHER group id
// first; Heise is the outlier, and its opt-in must prove solution-LENGTH
// parity (not solution equality).
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
  //
  // VALIDATION (the general constructor is the entry point every method
  // track uses, so a malformed declaration must fail loudly rather than
  // corrupt memory or silently mis-prune):
  //
  //   * REJECTED with `std::invalid_argument`:
  //       - a group id outside [0, kMaxGroupId] (a NEGATIVE id would wrap to
  //         a huge size_t and write out of bounds -- finding M-1);
  //       - a duplicate group id (a silent "last declaration wins" would
  //         hide a caller bug -- finding M-2);
  //       - a group whose `opposite` points at ITSELF (a group is never
  //         opposite to itself -- finding M-2);
  //       - an `opposite` below the -1 "no partner" sentinel;
  //       - a negative `move_group` entry (two moves carrying -1 would
  //         compare EQUAL under the same-group rule and wrongly prune a
  //         non-redundant pair, which can lengthen the optimal solution --
  //         finding M-3).
  //
  //   * ACCEPTED, with the documented behaviour:
  //       - a one-sided opposite declaration (e.g. `Group{0, 3}` with group 3
  //         not declared): the relation is normalised SYMMETRICALLY, so the
  //         pair {0, 3} still prunes exactly one of the two commuting
  //         orders -- never both, never neither (finding M-2);
  //       - a group id referenced by `move_group` but not declared (and not
  //         named as anybody's opposite partner): such a group is INERT --
  //         it participates in no rule at all, so two moves carrying it are
  //         NOT mutually redundant and the group has no opposite partner
  //         (finding M-3).  Declaring the group (with `opposite = -1` for
  //         "no partner") is what makes it live.
  //
  //   The opposite partner of a declared group is itself treated as live:
  //   declaring `Group{0, 3}` asserts that groups 0 and 3 are opposite
  //   faces, so both must participate in the rules.
  MovePruningTable(const std::vector<int>& allowed_moves,
                   const std::vector<int>& move_group,
                   const std::vector<Group>& groups);

  // Returns true if `curr` is redundant after `prev` (should be pruned).
  // Both `prev` and `curr` are move indices in this table's move space.
  // Out-of-range indices are never redundant.
  [[nodiscard]] bool isRedundant(int prev, int curr) const noexcept {
    if (!redundant_) return false;
    if (prev < 0 || prev >= num_moves_ || curr < 0 || curr >= num_moves_) {
      return false;
    }
    const std::vector<std::uint8_t>& table = *redundant_;
    const std::size_t base =
        static_cast<std::size_t>(prev) * static_cast<std::size_t>(num_moves_);
    return table[base + static_cast<std::size_t>(curr)] != 0;
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

  // View of this table as a searcher pruning policy.
  //
  // The returned policy SHARES the table's byte buffer, so the buffer stays
  // alive for as long as the policy does: destroying the table while the
  // searcher holds the policy can no longer cause a use-after-free.
  //
  // LIFETIME AND INDEX-SPACE CONTRACT (the producer side; the consumer side
  // is `core::IdaSearcher::setMovePruning`):
  //   * The table must remain alive, and must not be moved-from, destroyed,
  //     reassigned or rebuilt, for as long as any policy derived from it is
  //     in use by an `IdaSearcher`.
  //   * A policy must be re-obtained from `policy()` after the table is moved
  //     or copied.
  //   * A policy obtained from a moved-from table is INACTIVE
  //     (`active() == false`) and silently disables pruning.
  //   * The table's move-index space must be the same index space as the
  //     move ids in the searcher's `allowed_` list; ids outside it are
  //     treated as "never redundant", so a mismatch silently disables
  //     pruning for those ids rather than failing loudly.
  [[nodiscard]] rubik::core::MovePruningPolicy policy() const noexcept {
    return rubik::core::MovePruningPolicy(redundant_, num_moves_);
  }

 private:
  // Marks (prev, curr) redundant under the two Korf rules.
  //
  // `group_opposite` is the raw declaration; it is normalised into disjoint,
  // symmetric pairs.  `group_declared` marks the group ids that are LIVE
  // (declared in `groups`, or named as somebody's opposite partner); a move
  // whose group id is not live participates in no rule.
  void build(const std::vector<int>& move_group,
             const std::vector<int>& group_opposite,
             const std::vector<bool>& group_declared);

  std::vector<int> allowed_moves_;
  // num_moves_ x num_moves_, row-major.  Shared with every policy derived
  // from this table.
  std::shared_ptr<const std::vector<std::uint8_t>> redundant_;
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
  // All six faces are live groups.
  std::vector<bool> group_declared(6, true);
  build(move_group, group_opposite, group_declared);
}

inline MovePruningTable::MovePruningTable(
    const std::vector<int>& allowed_moves,
    const std::vector<int>& move_group,
    const std::vector<Group>& groups)
    : allowed_moves_(allowed_moves) {
  // -- Validation (findings M-1, M-2, M-3) --------------------------------
  // Everything is checked BEFORE the group table is allocated or indexed, so
  // a malformed declaration can never write out of bounds.
  for (const auto& g : groups) {
    if (g.id < 0) {
      throw std::invalid_argument(
          "MovePruningTable: a group id must be >= 0 (got " +
          std::to_string(g.id) + ")");
    }
    assert(g.id >= 0 && "a group id must be non-negative");
    if (g.id > kMaxGroupId) {
      throw std::invalid_argument(
          "MovePruningTable: a group id must be <= kMaxGroupId (got " +
          std::to_string(g.id) + ")");
    }
    if (g.opposite < -1) {
      throw std::invalid_argument(
          "MovePruningTable: a group opposite must be >= -1 (got " +
          std::to_string(g.opposite) + ")");
    }
    if (g.opposite == g.id) {
      throw std::invalid_argument(
          "MovePruningTable: a group cannot be its own opposite (id " +
          std::to_string(g.id) + ")");
    }
    if (g.opposite > kMaxGroupId) {
      throw std::invalid_argument(
          "MovePruningTable: a group opposite must be <= kMaxGroupId (got " +
          std::to_string(g.opposite) + ")");
    }
  }
  // Duplicate group ids are rejected rather than silently resolved.
  for (std::size_t i = 0; i < groups.size(); ++i) {
    for (std::size_t j = i + 1; j < groups.size(); ++j) {
      if (groups[i].id == groups[j].id) {
        throw std::invalid_argument(
            "MovePruningTable: duplicate group id " +
            std::to_string(groups[i].id));
      }
    }
  }
  // A negative move_group entry would make two unrelated moves compare equal
  // under the same-group rule, wrongly pruning a non-redundant pair.
  for (std::size_t m = 0; m < move_group.size(); ++m) {
    if (move_group[m] < 0) {
      throw std::invalid_argument(
          "MovePruningTable: a move_group entry must be >= 0 (move " +
          std::to_string(m) + " has group " + std::to_string(move_group[m]) +
          ")");
    }
  }

  // The group table must cover every declared id AND every opposite partner
  // id, because the relation is normalised symmetrically.
  int max_group = -1;
  for (const auto& g : groups) {
    if (g.id > max_group) max_group = g.id;
    if (g.opposite > max_group) max_group = g.opposite;
  }

  std::vector<int> group_opposite(static_cast<std::size_t>(max_group + 1), -1);
  std::vector<bool> group_declared(static_cast<std::size_t>(max_group + 1),
                                   false);
  for (const auto& g : groups) {
    group_opposite[static_cast<std::size_t>(g.id)] = g.opposite;
    group_declared[static_cast<std::size_t>(g.id)] = true;
    // The opposite partner is live too: declaring `Group{0, 3}` asserts that
    // groups 0 and 3 are opposite faces.
    if (g.opposite >= 0) {
      group_declared[static_cast<std::size_t>(g.opposite)] = true;
    }
  }
  build(move_group, group_opposite, group_declared);
}

inline void MovePruningTable::build(const std::vector<int>& move_group,
                                     const std::vector<int>& group_opposite,
                                     const std::vector<bool>& group_declared) {
  num_moves_ = static_cast<int>(move_group.size());

  // Normalise the declared opposite relation into disjoint, SYMMETRIC
  // pairs.
  //
  // A pair {a, b} becomes opposite[a] = b and opposite[b] = a.  With a
  // symmetric relation, `oppositeOf(prev) == curr && prev > curr` rejects
  // exactly one of the two commuting orders -- never both, never neither
  // (finding M-2, acceptance criterion F-14).  A one-sided declaration
  // (`Group{0, 3}` with group 3 undeclared) is normalised here too, so it
  // still prunes exactly one order.
  //
  // A group that is claimed by two different pairs keeps the first pair it
  // was assigned (lowest group id wins); the conflicting declaration is
  // dropped.
  std::vector<int> opposite(group_opposite.size(), -1);
  for (std::size_t g = 0; g < group_opposite.size(); ++g) {
    const int other = group_opposite[g];
    if (other < 0 || other >= static_cast<int>(group_opposite.size())) continue;
    if (static_cast<std::size_t>(other) == g) continue;  // never opposite to itself
    const std::size_t lo = std::min(g, static_cast<std::size_t>(other));
    const std::size_t hi = std::max(g, static_cast<std::size_t>(other));
    if (opposite[lo] != -1 || opposite[hi] != -1) continue;  // already paired
    opposite[lo] = static_cast<int>(hi);
    opposite[hi] = static_cast<int>(lo);
  }

  std::vector<std::uint8_t> table(
      static_cast<std::size_t>(num_moves_) * static_cast<std::size_t>(num_moves_),
      0);

  auto groupOf = [&](int move) -> int {
    if (move < 0 || move >= num_moves_) return -1;
    const int group = move_group[static_cast<std::size_t>(move)];
    // A NEGATIVE group id means "this move belongs to no group": the move
    // takes part in no rule at all.  Without this, two moves carrying an
    // invalid group id would look like "the same group" to the same-group
    // rule and would wrongly prune each other.
    if (group < 0) return -1;
    // A group id that is not LIVE is inert as well: declaring the group is
    // what makes it participate in the rules.
    if (group >= static_cast<int>(group_declared.size())) return -1;
    if (!group_declared[static_cast<std::size_t>(group)]) return -1;
    return group;
  };
  auto oppositeOf = [&](int group) -> int {
    if (group < 0 || group >= static_cast<int>(opposite.size())) return -1;
    return opposite[static_cast<std::size_t>(group)];
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
        table[base + static_cast<std::size_t>(curr)] = 1;
      }
    }
  }

  redundant_ = std::shared_ptr<const std::vector<std::uint8_t>>(
      std::make_shared<std::vector<std::uint8_t>>(std::move(table)));
}

}  // namespace rubik::tables
