#include <vector>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "tables/move_pruning.hpp"
#include "tables/stage_config.hpp"
#include "core/moves.hpp"

using rubik::tables::MovePruningTable;
using rubik::tables::all_18_face_move_indices;
using rubik::all_face_moves;

int g_failures = 0;
#define CHECK(cond, msg)                                       \
  do {                                                         \
    if (!(cond)) {                                             \
      std::cerr << "FAIL: " << msg << "\n";                    \
      ++g_failures;                                            \
    } else {                                                   \
      std::cout << "ok: " << msg << "\n";                      \
    }                                                          \
  } while (0)

void testSameFacePruning() {
  // Full 18-move set: same-face moves should be redundant.
  MovePruningTable table(rubik::tables::all_18_face_move_indices());
  
  // U (0) followed by U' (1), U2 (2) should all be redundant.
  CHECK(table.isRedundant(0, 1), "U -> U' redundant");
  CHECK(table.isRedundant(0, 2), "U -> U2 redundant");
  CHECK(table.isRedundant(1, 0), "U' -> U redundant");
  CHECK(table.isRedundant(1, 2), "U' -> U2 redundant");
  CHECK(table.isRedundant(2, 0), "U2 -> U redundant");
  CHECK(table.isRedundant(2, 1), "U2 -> U' redundant");
  
  // Same for R face (3,4,5)
  CHECK(table.isRedundant(3, 4), "R -> R' redundant");
  CHECK(table.isRedundant(3, 5), "R -> R2 redundant");
  CHECK(table.isRedundant(4, 3), "R' -> R redundant");
  CHECK(table.isRedundant(4, 5), "R' -> R2 redundant");
  CHECK(table.isRedundant(5, 3), "R2 -> R redundant");
  CHECK(table.isRedundant(5, 4), "R2 -> R' redundant");
  
  // Different faces should NOT be redundant (except opposite face ordering).
  CHECK(!table.isRedundant(0, 3), "U -> R not redundant");
  CHECK(!table.isRedundant(3, 0), "R -> U not redundant");
}

void testOppositeFaceOrdering() {
  MovePruningTable table(rubik::tables::all_18_face_move_indices());
  
  // Opposite faces: U(0) vs D(3), F(2) vs B(5), L(4) vs R(1)
  // Lower face index first is allowed; higher first is redundant.
  
  // U (face 0) vs D (face 3): U first allowed, D first redundant
  CHECK(!table.isRedundant(0, 9), "U -> D allowed (U face 0 < D face 3)");
  CHECK(table.isRedundant(9, 0), "D -> U redundant (D face 3 > U face 0)");
  CHECK(!table.isRedundant(0, 10), "U -> D' allowed");
  CHECK(table.isRedundant(10, 0), "D' -> U redundant");
  CHECK(!table.isRedundant(0, 11), "U -> D2 allowed");
  CHECK(table.isRedundant(11, 0), "D2 -> U redundant");
  
  // F (face 2) vs B (face 5): F first allowed, B first redundant
  CHECK(!table.isRedundant(6, 15), "F -> B allowed (F face 2 < B face 5)");
  CHECK(table.isRedundant(15, 6), "B -> F redundant (B face 5 > F face 2)");
  
  // L (face 4) vs R (face 1): R first allowed (face 1 < 4), L first redundant
  CHECK(!table.isRedundant(3, 12), "R -> L allowed (R face 1 < L face 4)");
  CHECK(table.isRedundant(12, 3), "L -> R redundant (L face 4 > R face 1)");
}

void testSubsetMoveSet() {
  // Test with a subset (e.g., R/L/U only for ZZ)
  std::vector<int> rlu_moves = {0, 1, 2, 3, 4, 5, 12, 13, 14};  // U, R, L
  MovePruningTable table(rlu_moves);
  
  // Same-face pruning still works within subset
  CHECK(table.isRedundant(0, 1), "U -> U' redundant in subset");
  CHECK(table.isRedundant(3, 4), "R -> R' redundant in subset");
  CHECK(table.isRedundant(12, 13), "L -> L' redundant in subset");
  
  // Opposite faces within subset: U vs D (D not in subset), F vs B (not in subset), L vs R
  // R (face 1) vs L (face 4): R first allowed
  CHECK(!table.isRedundant(3, 12), "R -> L allowed (R face 1 < L face 4)");
  CHECK(table.isRedundant(12, 3), "L -> R redundant (L face 4 > R face 1)");
  
  // Moves not in subset should not be checked (but isRedundant returns false for them)
  CHECK(!table.isRedundant(0, 9), "U -> D (D not in subset) -> not checked, returns false");
}

void testFilterMoves() {
  MovePruningTable table(rubik::tables::all_18_face_move_indices());
  
  // After U (0), allowed moves should exclude U/U'/U2 (same face).
  // D moves should be KEPT because U (face 0) < D (face 3) - canonical order.
  auto filtered = table.filterMoves(0);
  CHECK(std::find(filtered.begin(), filtered.end(), 0) == filtered.end(), "filterMoves: U removed after U");
  CHECK(std::find(filtered.begin(), filtered.end(), 1) == filtered.end(), "filterMoves: U' removed after U");
  CHECK(std::find(filtered.begin(), filtered.end(), 2) == filtered.end(), "filterMoves: U2 removed after U");
  // D moves are KEPT after U because U < D (canonical order: lower face first)
  CHECK(std::find(filtered.begin(), filtered.end(), 9) != filtered.end(), "filterMoves: D kept after U (U<D)");
  CHECK(std::find(filtered.begin(), filtered.end(), 10) != filtered.end(), "filterMoves: D' kept after U");
  CHECK(std::find(filtered.begin(), filtered.end(), 11) != filtered.end(), "filterMoves: D2 kept after U");
  
  // R (3), R' (4), R2 (5) should remain
  CHECK(std::find(filtered.begin(), filtered.end(), 3) != filtered.end(), "filterMoves: R kept after U");
  CHECK(std::find(filtered.begin(), filtered.end(), 4) != filtered.end(), "filterMoves: R' kept after U");
  CHECK(std::find(filtered.begin(), filtered.end(), 5) != filtered.end(), "filterMoves: R2 kept after U");
  
  // After D (9), U moves should be REMOVED (D > U, non-canonical).
  // D moves removed (same face).
  filtered = table.filterMoves(9);
  CHECK(std::find(filtered.begin(), filtered.end(), 0) == filtered.end(), "filterMoves: U removed after D (D>U)");
  CHECK(std::find(filtered.begin(), filtered.end(), 9) == filtered.end(), "filterMoves: D removed after D");
  CHECK(std::find(filtered.begin(), filtered.end(), 10) == filtered.end(), "filterMoves: D' removed after D");
  CHECK(std::find(filtered.begin(), filtered.end(), 11) == filtered.end(), "filterMoves: D2 removed after D");
}

void testNoPruningAtRoot() {
  // prev = -1 (root) should not prune anything
  MovePruningTable table(rubik::tables::all_18_face_move_indices());
  // isRedundant with prev=-1 is not called by searcher; but if called, returns false
  // (our implementation returns false for out-of-range indices)
  CHECK(!table.isRedundant(-1, 0), "prev=-1 -> no pruning");
  CHECK(!table.isRedundant(-1, 9), "prev=-1 -> no pruning");
}

// ---------------------------------------------------------------------------
// F-01 ratification: the canonical opposite-face direction.
//
// The shared table keeps the LOWER group id first for an opposite pair:
// for {U(0), D(3)} the order U -> D is kept and D -> U is rejected.
//
// This is deliberately NOT flipped.  The pre-9c87376 table used the same rule
// (`prev_face > curr_face`), and the committed tests above already pin it
// (R -> L allowed / L -> R redundant).  Heise keeps the HIGHER group id
// first; Heise is the outlier.
// ---------------------------------------------------------------------------
void testCanonicalOppositeDirectionIsLowerGroupIdFirst() {
  MovePruningTable table(rubik::tables::all_18_face_move_indices());

  // The three opposite pairs, in (lower group id, higher group id) order.
  const int pairs[3][2] = {{0, 3}, {1, 4}, {2, 5}};

  for (const auto& pair : pairs) {
    const int lo = pair[0];
    const int hi = pair[1];
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        const int a = lo * 3 + i;  // a move of the lower-id face
        const int b = hi * 3 + j;  // a move of the higher-id face
        CHECK(!table.isRedundant(a, b),
              "lower group id first is kept (" + std::to_string(a) + " -> " +
                  std::to_string(b) + ")");
        CHECK(table.isRedundant(b, a),
              "higher group id first is rejected (" + std::to_string(b) + " -> " +
                  std::to_string(a) + ")");
      }
    }
  }

  // The exact assertions the committed tests already make, re-checked here so
  // that the ratification is pinned by a named regression test.
  CHECK(!table.isRedundant(3, 12), "R -> L allowed (R face 1 < L face 4)");
  CHECK(table.isRedundant(12, 3), "L -> R redundant (L face 4 > R face 1)");
  CHECK(!table.isRedundant(0, 9), "U -> D allowed (U face 0 < D face 3)");
  CHECK(table.isRedundant(9, 0), "D -> U redundant (D face 3 > U face 0)");
  CHECK(!table.isRedundant(6, 15), "F -> B allowed (F face 2 < B face 5)");
  CHECK(table.isRedundant(15, 6), "B -> F redundant (B face 5 > F face 2)");
}

// ---------------------------------------------------------------------------
// New layout: the general constructor and the generalised table.
// ---------------------------------------------------------------------------

// A 21-move space: the 18 face moves plus Roux's M slice (M, M', M2) as
// group 6 with NO opposite partner.
struct MoveSpace21 {
  std::vector<int> allowed;
  std::vector<int> move_group;
  std::vector<MovePruningTable::Group> groups;

  MoveSpace21() {
    allowed = rubik::tables::all_18_face_move_indices();
    allowed.push_back(18);
    allowed.push_back(19);
    allowed.push_back(20);
    move_group.resize(21);
    for (int i = 0; i < 18; ++i) {
      move_group[static_cast<std::size_t>(i)] = rubik::tables::kMoveFace[static_cast<std::size_t>(i)];
    }
    move_group[18] = 6;
    move_group[19] = 6;
    move_group[20] = 6;
    groups.push_back({0, 3});
    groups.push_back({1, 4});
    groups.push_back({2, 5});
    groups.push_back({6, -1});  // the M slice has no opposite partner
  }
};

void testGeneralConstructor() {
  // An explicit move space: 4 moves in 2 groups, with the groups declared
  // opposite to each other.
  const std::vector<int> allowed = {0, 1, 2, 3};
  const std::vector<int> move_group = {0, 0, 1, 1};
  const std::vector<MovePruningTable::Group> groups = {{0, 1}, {1, 0}};
  MovePruningTable table(allowed, move_group, groups);

  CHECK(table.numMoves() == 4, "general constructor: numMoves == move_group.size()");
  CHECK(table.allowedMoves() == allowed, "general constructor: allowedMoves preserved");

  // Same group -> redundant.
  CHECK(table.isRedundant(0, 1), "general: same group 0 redundant");
  CHECK(table.isRedundant(2, 3), "general: same group 1 redundant");

  // Opposite groups: only the lower group id goes first.
  CHECK(!table.isRedundant(0, 2), "general: group 0 -> group 1 kept (lower id first)");
  CHECK(table.isRedundant(2, 0), "general: group 1 -> group 0 redundant (higher id first)");
}

void test21MoveSpaceWithMSlice() {
  MoveSpace21 space;
  MovePruningTable table(space.allowed, space.move_group, space.groups);

  CHECK(table.numMoves() == 21, "21-move space: numMoves == 21");
  CHECK(table.allowedMoves().size() == 21, "21-move space: allowedMoves has 21 entries");
  CHECK(table.allowedMoves()[18] == 18 && table.allowedMoves()[19] == 19 &&
            table.allowedMoves()[20] == 20,
        "21-move space: M, M', M2 are the last three moves");

  // The M slice is a group of its own: same-group redundancy applies.
  CHECK(table.isRedundant(18, 19), "M -> M' redundant (same group 6)");
  CHECK(table.isRedundant(18, 20), "M -> M2 redundant (same group 6)");
  CHECK(table.isRedundant(19, 18), "M' -> M redundant (same group 6)");
  CHECK(table.isRedundant(19, 20), "M' -> M2 redundant (same group 6)");
  CHECK(table.isRedundant(20, 18), "M2 -> M redundant (same group 6)");
  CHECK(table.isRedundant(20, 19), "M2 -> M' redundant (same group 6)");

  // The M slice has NO opposite partner, so it takes part only in the
  // same-group rule -- it is never pruned against a face, even one that
  // commutes with it (M commutes with U, but they are not opposite faces).
  CHECK(!table.isRedundant(18, 0), "M -> U not redundant (M has no opposite)");
  CHECK(!table.isRedundant(0, 18), "U -> M not redundant (M has no opposite)");
  CHECK(!table.isRedundant(18, 12), "M -> L not redundant (M has no opposite)");
  CHECK(!table.isRedundant(12, 18), "L -> M not redundant (M has no opposite)");
  CHECK(!table.isRedundant(18, 15), "M -> B not redundant (M has no opposite)");

  // The face moves still behave exactly as in the 18-face table.
  CHECK(!table.isRedundant(0, 9), "21-space: U -> D allowed (canonical order)");
  CHECK(table.isRedundant(9, 0), "21-space: D -> U redundant");
  CHECK(!table.isRedundant(3, 12), "21-space: R -> L allowed");
  CHECK(table.isRedundant(12, 3), "21-space: L -> R redundant");
}

void testNumMovesAndAllowedMoves() {
  MovePruningTable table(rubik::tables::all_18_face_move_indices());
  CHECK(table.numMoves() == 18, "18-face table: numMoves == 18");
  CHECK(table.allowedMoves() == rubik::tables::all_18_face_move_indices(),
        "18-face table: allowedMatches the requested move set");

  // A subset move set keeps its own allowed list but the table's move space
  // is still the 18 face moves.
  const std::vector<int> rlu = {0, 1, 2, 3, 4, 5, 12, 13, 14};
  MovePruningTable subset(rlu);
  CHECK(subset.numMoves() == 18, "subset: numMoves is still the move space size");
  CHECK(subset.allowedMoves() == rlu, "subset: allowedMoves is the requested subset");
}

void testFilterMovesGeneralSpace() {
  MoveSpace21 space;
  MovePruningTable table(space.allowed, space.move_group, space.groups);

  // After M (18): the other M moves are pruned, everything else survives.
  auto filtered = table.filterMoves(18);
  CHECK(std::find(filtered.begin(), filtered.end(), 18) == filtered.end(),
        "filterMoves(18): M removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 19) == filtered.end(),
        "filterMoves(18): M' removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 20) == filtered.end(),
        "filterMoves(18): M2 removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 0) != filtered.end(),
        "filterMoves(18): U kept");
  CHECK(std::find(filtered.begin(), filtered.end(), 9) != filtered.end(),
        "filterMoves(18): D kept");

  // After U (0): the U moves are pruned, the D moves survive (canonical
  // order), and the L moves survive (L is not opposite to U).
  filtered = table.filterMoves(0);
  CHECK(std::find(filtered.begin(), filtered.end(), 0) == filtered.end(),
        "filterMoves(0): U removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 1) == filtered.end(),
        "filterMoves(0): U' removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 2) == filtered.end(),
        "filterMoves(0): U2 removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 9) != filtered.end(),
        "filterMoves(0): D kept (U < D)");
  CHECK(std::find(filtered.begin(), filtered.end(), 12) != filtered.end(),
        "filterMoves(0): L kept (L is not opposite to U)");
  CHECK(std::find(filtered.begin(), filtered.end(), 18) != filtered.end(),
        "filterMoves(0): M kept (M has no opposite)");

  // After L (12): the R moves are pruned (higher group id first is rejected).
  filtered = table.filterMoves(12);
  CHECK(std::find(filtered.begin(), filtered.end(), 3) == filtered.end(),
        "filterMoves(12): R removed (L face 4 > R face 1)");
  CHECK(std::find(filtered.begin(), filtered.end(), 4) == filtered.end(),
        "filterMoves(12): R' removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 5) == filtered.end(),
        "filterMoves(12): R2 removed");
  CHECK(std::find(filtered.begin(), filtered.end(), 12) == filtered.end(),
        "filterMoves(12): L removed (same face)");
}

void testOutOfRangeIndices() {
  MoveSpace21 space;
  MovePruningTable table(space.allowed, space.move_group, space.groups);

  // Negative indices are never redundant.
  CHECK(!table.isRedundant(-1, 0), "negative prev is never redundant");
  CHECK(!table.isRedundant(0, -1), "negative curr is never redundant");
  CHECK(!table.isRedundant(-1, -1), "both negative is never redundant");

  // Indices beyond the move space are never redundant.
  CHECK(!table.isRedundant(21, 0), "prev beyond the move space is never redundant");
  CHECK(!table.isRedundant(0, 21), "curr beyond the move space is never redundant");
  CHECK(!table.isRedundant(1000, 1000), "far out-of-range is never redundant");
}

// F-02 / F-14: for every opposite pair exactly ONE of the two commuting
// orders is marked redundant -- never both, never neither.  Forbidding BOTH
// would delete every adjacency between the two commuting faces and could
// lengthen the optimal solution.
void testExactlyOneOrderPerOppositePair() {
  {
    MovePruningTable table(rubik::tables::all_18_face_move_indices());
    const int pairs[3][2] = {{0, 3}, {1, 4}, {2, 5}};
    for (const auto& pair : pairs) {
      for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
          const int a = pair[0] * 3 + i;
          const int b = pair[1] * 3 + j;
          const int forward = table.isRedundant(a, b) ? 1 : 0;
          const int backward = table.isRedundant(b, a) ? 1 : 0;
          CHECK(forward + backward == 1,
                "18-face: exactly one of (" + std::to_string(a) + " -> " +
                    std::to_string(b) + ") and (" + std::to_string(b) + " -> " +
                    std::to_string(a) + ") is redundant");
        }
      }
    }
  }
  {
    MoveSpace21 space;
    MovePruningTable table(space.allowed, space.move_group, space.groups);
    const int pairs[3][2] = {{0, 3}, {1, 4}, {2, 5}};
    for (const auto& pair : pairs) {
      for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
          const int a = pair[0] * 3 + i;
          const int b = pair[1] * 3 + j;
          const int forward = table.isRedundant(a, b) ? 1 : 0;
          const int backward = table.isRedundant(b, a) ? 1 : 0;
          CHECK(forward + backward == 1,
                "21-space: exactly one of (" + std::to_string(a) + " -> " +
                    std::to_string(b) + ") and (" + std::to_string(b) + " -> " +
                    std::to_string(a) + ") is redundant");
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Findings M-1 / M-2 / M-3: the general constructor's validation and the
// structural guarantees the pruning rule relies on.
//
//   M-1  a negative Group::id used to wrap to SIZE_MAX and write out of
//        bounds -- it must be rejected BEFORE the group table is allocated
//        and indexed.
//   M-2  the opposite relation must be normalised SYMMETRICALLY, so that
//        exactly one of the two commuting orders is rejected -- never both,
//        never neither (acceptance criterion F-14).
//   M-3  the move-index space and the group-id space must be cross-validated:
//        a negative move_group entry must never make two unrelated moves
//        compare equal under the same-group rule (which would prune a
//        NON-redundant pair and can lengthen the optimal solution).
// ---------------------------------------------------------------------------
void testNegativeGroupIdIsRejected() {
  const std::vector<int> allowed = {0, 1, 2, 3};
  const std::vector<int> move_group = {0, 0, 1, 1};

  bool threw = false;
  try {
    MovePruningTable table(allowed, move_group, {{-1, 0}});
    (void)table;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw, "M-1: a negative group id is rejected (no out-of-bounds write)");

  threw = false;
  try {
    MovePruningTable table(allowed, move_group, {{0, 1}, {1, 0}, {-1, 2}});
    (void)table;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw, "M-1: a negative group id is rejected next to valid ones");

  // A group id beyond the supported range is rejected too, so that the
  // group table can never be sized by an absurd value.
  threw = false;
  try {
    MovePruningTable table(allowed, move_group,
                           {{0, 1}, {1, 0}, {rubik::tables::kMaxGroupId + 1, 0}});
    (void)table;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw, "M-1: a group id beyond kMaxGroupId is rejected");
}

void testOppositeRelationIsSymmetric() {
  const std::vector<int> allowed = {0, 1, 2, 3};
  const std::vector<int> move_group = {0, 0, 1, 1};

  // (a) One-sided declaration: group 0 declares group 1 as its opposite and
  //     group 1 is not declared.  The relation is normalised symmetrically,
  //     so the pair still rejects EXACTLY ONE order.
  {
    MovePruningTable table(allowed, move_group, {{0, 1}});
    CHECK(!table.isRedundant(0, 2),
          "M-2(a): one-sided declaration keeps the lower group id first");
    CHECK(table.isRedundant(2, 0),
          "M-2(a): one-sided declaration rejects the higher group id first");
  }

  // (b) Symmetric declaration: the same result.
  {
    MovePruningTable table(allowed, move_group, {{0, 1}, {1, 0}});
    CHECK(!table.isRedundant(0, 2),
          "M-2(b): symmetric declaration keeps the lower group id first");
    CHECK(table.isRedundant(2, 0),
          "M-2(b): symmetric declaration rejects the higher group id first");
  }

  // (c) A self-opposite group is rejected.
  bool threw = false;
  try {
    MovePruningTable table(allowed, move_group, {{0, 0}, {1, -1}});
    (void)table;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw, "M-2(c): a self-opposite group is rejected");

  // (d) A duplicate group id is rejected rather than silently last-wins.
  threw = false;
  try {
    MovePruningTable table(allowed, move_group, {{0, 1}, {0, -1}});
    (void)table;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw, "M-2(d): a duplicate group id is rejected");
}

void testMoveGroupIsCrossValidated() {
  const std::vector<int> allowed = {0, 1, 2, 3};
  const std::vector<int> move_group = {0, 0, 1, 1};

  // (a) A negative move_group entry is rejected: it would make two
  //     unrelated moves compare EQUAL under the same-group rule, wrongly
  //     pruning a non-redundant pair (which can lengthen the optimal
  //     solution).  No table is produced, so no pair can be pruned.
  bool threw = false;
  try {
    MovePruningTable table(allowed, {-1, -1, 1, 1}, {{0, 1}, {1, 0}});
    (void)table;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw, "M-3(a): a negative move_group entry is rejected");

  // (b) A move_group entry naming an UNDECLARED group id is inert: the
  //     group participates in no rule at all, so two distinct moves that
  //     carry it are NOT mutually redundant, and the group gets no
  //     opposite pruning.  (This is the same code path that protects the
  //     negative case above.)
  {
    const std::vector<int> undeclared = {5, 5, 0, 1};
    MovePruningTable table(allowed, undeclared, {{0, 1}, {1, 0}});
    CHECK(!table.isRedundant(0, 1),
          "M-3(b): moves in an undeclared group are NOT mutually redundant");
    CHECK(!table.isRedundant(1, 0),
          "M-3(b): moves in an undeclared group are NOT mutually redundant "
          "(reverse)");
    CHECK(!table.isRedundant(0, 2),
          "M-3(b): an undeclared group gets no opposite pruning");
    CHECK(!table.isRedundant(2, 0),
          "M-3(b): an undeclared group gets no opposite pruning (reverse)");
    // The DECLARED groups still work.
    CHECK(!table.isRedundant(2, 3),
          "M-3(b): the declared opposite pair keeps the lower id first");
    CHECK(table.isRedundant(3, 2),
          "M-3(b): the declared opposite pair rejects the higher id first");
  }
}

// Malformed input must never cause an out-of-bounds access and must always
// yield a defined result.
void testMalformedGeneralConstructor() {
  const std::vector<int> allowed = {0, 1, 2, 3};
  const std::vector<int> move_group = {0, 0, 1, 1};
  const std::vector<MovePruningTable::Group> groups = {{0, 1}, {1, 0}};

  // A move_group whose size differs from the number of allowed moves: the
  // move space is `move_group.size()`, and the extra allowed moves are
  // simply outside it -- never redundant, never pruned.
  {
    const std::vector<int> six_moves = {0, 1, 2, 3, 4, 5};
    MovePruningTable table(six_moves, move_group, groups);
    CHECK(table.numMoves() == 4,
          "short move_group: the move space is move_group.size()");
    CHECK(table.allowedMoves() == six_moves,
          "short move_group: allowedMoves still lists every requested move");
    CHECK(!table.isRedundant(4, 0),
          "short move_group: a move outside the space is never redundant");
    CHECK(!table.isRedundant(0, 4),
          "short move_group: a move outside the space is never redundant");
    const auto filtered = table.filterMoves(0);
    CHECK(std::find(filtered.begin(), filtered.end(), 4) != filtered.end(),
          "short move_group: a move outside the space is never pruned");
    CHECK(std::find(filtered.begin(), filtered.end(), 5) != filtered.end(),
          "short move_group: a move outside the space is never pruned");
  }

  // An empty group list: no group is live, so no rule applies at all.
  {
    MovePruningTable table(allowed, move_group, {});
    CHECK(table.numMoves() == 4, "no groups: the table is still built");
    CHECK(!table.isRedundant(0, 1),
          "no groups: no group is live, so the same-group rule does not fire");
    CHECK(!table.isRedundant(2, 0),
          "no groups: no opposite pruning either");
  }

  // A move whose group id is negative takes part in no rule at all.
  {
    const std::vector<int> bad_group = {-1, -1, 1, 1};
    const std::vector<MovePruningTable::Group> ok_groups = {{0, 1}, {1, 0}};
    bool threw = false;
    try {
      MovePruningTable table(allowed, bad_group, ok_groups);
      (void)table;
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    CHECK(threw, "negative group id on a move is rejected");
  }
}

// The policy adapter: a non-owning, read-only view that shares the table's
// buffer.
void testPolicyAdapter() {
  MovePruningTable table(rubik::tables::all_18_face_move_indices());
  const auto policy = table.policy();

  CHECK(policy.active(), "policy: an 18-face table yields an active policy");

  bool agrees = true;
  for (int p = 0; p < 18; ++p) {
    for (int c = 0; c < 18; ++c) {
      if (policy.isRedundant(p, c) != table.isRedundant(p, c)) agrees = false;
    }
  }
  CHECK(agrees, "policy: agrees with the table for every in-range pair");

  CHECK(!policy.isRedundant(-1, 0), "policy: negative prev is never redundant");
  CHECK(!policy.isRedundant(0, -1), "policy: negative curr is never redundant");
  CHECK(!policy.isRedundant(0, 999), "policy: out-of-range curr is never redundant");
  CHECK(!policy.isRedundant(999, 0), "policy: out-of-range prev is never redundant");

  // A default-constructed table yields an INACTIVE policy.
  CHECK(!MovePruningTable().policy().active(),
        "policy: a default table yields an inactive policy");

  // A policy obtained BEFORE the table is moved stays valid by
  // construction: it shares the table's byte buffer, so the buffer cannot
  // be freed while the policy lives.
  MovePruningTable source(rubik::tables::all_18_face_move_indices());
  const auto before = source.policy();
  CHECK(before.active(), "policy: active before the move");
  MovePruningTable dest = std::move(source);
  CHECK(before.active(), "policy: still active after the table was moved");
  CHECK(before.isRedundant(12, 3) == dest.isRedundant(12, 3),
        "policy: a policy taken before a move still answers identically");
  CHECK(before.isRedundant(12, 3),
        "policy: a policy taken before a move still prunes correctly");
  // A policy taken from a MOVED-FROM table is inactive.
  CHECK(!source.policy().active(),
        "policy: a moved-from table yields an inactive policy");
}

int main() {
  testSameFacePruning();
  testOppositeFaceOrdering();
  testSubsetMoveSet();
  testFilterMoves();
  testNoPruningAtRoot();
  testCanonicalOppositeDirectionIsLowerGroupIdFirst();
  testGeneralConstructor();
  test21MoveSpaceWithMSlice();
  testNumMovesAndAllowedMoves();
  testFilterMovesGeneralSpace();
  testOutOfRangeIndices();
  testExactlyOneOrderPerOppositePair();
  testNegativeGroupIdIsRejected();
  testOppositeRelationIsSymmetric();
  testMoveGroupIsCrossValidated();
  testMalformedGeneralConstructor();
  testPolicyAdapter();
  
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}