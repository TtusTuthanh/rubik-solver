#include <vector>
#include <iostream>
#include <algorithm>

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

int main() {
  testSameFacePruning();
  testOppositeFaceOrdering();
  testSubsetMoveSet();
  testFilterMoves();
  testNoPruningAtRoot();
  
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}