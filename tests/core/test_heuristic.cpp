#include <vector>
#include <functional>
#include <iostream>

#include "core/heuristic.hpp"
#include "core/cube.hpp"

using rubik::core::PdbLookup;
using rubik::core::StageHeuristic;
using rubik::core::makeStageHeuristic;
using rubik::tables::Pattern;
using rubik::tables::DisjointGroup;
using rubik::tables::StageConfig;
using rubik::CubeState;

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

// Mock PDB that returns a fixed value for testing.
PdbLookup makeMockPdb(const std::string& name, int value, std::size_t num_entries = 1000) {
  std::vector<std::uint8_t> data((num_entries + 1) / 2, 0);
  // Pack value into all entries
  for (std::size_t i = 0; i < num_entries; ++i) {
    std::size_t byte_idx = i >> 1;
    std::uint8_t v = static_cast<std::uint8_t>(value & 0x0F);
    if ((i & 1) == 0) {
      data[byte_idx] = (data[byte_idx] & 0xF0) | v;
    } else {
      data[byte_idx] = (data[byte_idx] & 0x0F) | (v << 4);
    }
  }
  // Return a PdbLookup with a static buffer (leaked for test simplicity)
  static std::vector<std::vector<std::uint8_t>> storage;
  storage.push_back(std::move(data));
  const std::uint8_t* ptr = storage.back().data();
  
  return PdbLookup{
    .name = name,
    .data = ptr,
    .num_entries = num_entries,
    .index_fn = [](const CubeState&) -> std::uint32_t { return 0; }
  };
}

// The 9-move {U, R, L} alphabet used by ZZ's stages.
std::vector<int> zz_alphabet() {
  return {0, 1, 2, 3, 4, 5, 12, 13, 14};
}

// Patterns whose move-support sets are pairwise disjoint over {U, R, L}:
// "left" is only moved by L turns, "right" only by R turns, "coll" only by U
// turns.  Their piece sets are pairwise disjoint too.
std::vector<Pattern> summable_patterns() {
  return {
    Pattern{"left",  {0, 1}, {2}, true, 100, {12, 13, 14}},
    Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},
    Pattern{"coll",  {6},    {7}, true, 100, {0, 1, 2}}
  };
}

void testMaxCombinatorNoGroups() {
  // No disjoint groups -> MAX of all PDBs
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 3),
    makeMockPdb("pdb2", 5),
    makeMockPdb("pdb3", 2)
  };
  StageHeuristic h(pdbs, {});
  CubeState state = CubeState::solved();
  
  CHECK(h(state) == 5, "MAX combinator: max(3,5,2) = 5");
}

void testSumWithinGroupMaxAcrossGroups() {
  // BEFORE (asserted the UNSOUND sum): the same two groups were declared
  //   without an opt-in and without any declared move support, and the test
  //   asserted h == 7 (= 3 + 4).  That is the behaviour BUG-02 removes.
  // AFTER: the same arithmetic is exercised THROUGH the structural gate --
  //   group 0's constituents declare disjoint move supports over the stage's
  //   {U, R, L} alphabet AND the group is explicitly opted in, so the sum is
  //   used; group 1 is not opted in and is MAX-ed.  MAX across groups is
  //   unchanged.
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 3),
    makeMockPdb("pdb2", 4),
    makeMockPdb("pdb3", 6)
  };
  std::vector<DisjointGroup> groups = {
    DisjointGroup{{0, 1}, true},  // pdb1 + pdb2, opted in -> SUM
    DisjointGroup{{2}}            // pdb3 alone, not opted in -> MAX
  };
  StageHeuristic h(pdbs, groups, summable_patterns(), zz_alphabet());
  CubeState state = CubeState::solved();
  
  CHECK(h.groupIsSummed(0), "Group 0 summed: disjoint move supports + explicit opt-in");
  CHECK(!h.groupIsSummed(1), "Group 1 not summed: no explicit opt-in");
  CHECK(h(state) == 7, "Sum within group, max across: max(3+4, 6) = 7");
}

void testGroupMaxedWhenGateFails() {
  // The refusal path.  Two cases, both of which the old code got wrong:
  //   (a) piece-disjoint but the constituents SHARE move support: a single
  //       face turn advances both groups, so the sum can exceed the true
  //       joint distance -> MAX;
  //   (b) disjoint move supports but NO explicit opt-in: sum-ability is
  //       never inferred -> MAX.
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 3),
    makeMockPdb("pdb2", 4)
  };

  // (a) piece-disjoint, but both patterns are moved by the U face.
  std::vector<Pattern> shared_support = {
    Pattern{"left",  {0, 1}, {2}, true, 100, {0, 1, 2}},
    Pattern{"right", {3, 4}, {5}, true, 100, {0, 1, 2}}
  };
  std::vector<DisjointGroup> opted_in = {DisjointGroup{{0, 1}, true}};
  StageHeuristic h_shared(pdbs, opted_in, shared_support, zz_alphabet());
  CubeState state = CubeState::solved();

  CHECK(shared_support[0].all_piece_indices().size() == 3, "Case (a) patterns track 3 pieces each");
  CHECK(opted_in[0].verify_disjoint(shared_support), "Case (a) patterns are piece-disjoint");
  CHECK(!h_shared.groupIsSummed(0), "Case (a) not summed: move supports overlap");
  CHECK(h_shared(state) == 4, "Case (a) MAX(3,4) = 4, never 3+4 = 7");

  // (b) disjoint move supports, but the group was never opted in.
  std::vector<DisjointGroup> not_opted_in = {DisjointGroup{{0, 1}}};
  StageHeuristic h_not_opted(pdbs, not_opted_in, summable_patterns(), zz_alphabet());

  CHECK(!h_not_opted.groupIsSummed(0), "Case (b) not summed: no explicit opt-in");
  CHECK(h_not_opted(state) == 4, "Case (b) MAX(3,4) = 4, never 3+4 = 7");
}

void testMultipleGroups() {
  // BEFORE: three groups were summed unconditionally and the test asserted
  //   h == 7 (= max(2+3, 7, 1+4)).
  // AFTER: the same group structure is exercised through the structural
  //   gate.  Group 0 (2 + 3) and group 2 (1 + 4) declare pairwise-disjoint
  //   move supports over {U, R, L} and are explicitly opted in, so they are
  //   summed; group 1 is a single, non-opted-in pattern and is MAX-ed.
  //   The resulting maximum is unchanged: max(5, 7, 5) = 7.
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 2),
    makeMockPdb("pdb2", 3),
    makeMockPdb("pdb3", 7),
    makeMockPdb("pdb4", 1),
    makeMockPdb("pdb5", 4)
  };
  std::vector<Pattern> patterns = {
    Pattern{"left",  {0, 1}, {2}, true, 100, {12, 13, 14}},
    Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},
    Pattern{"coll",  {6},    {7}, true, 100, {0, 1, 2}},
    Pattern{"uedge", {2},    {8}, true, 100, {12, 13, 14}},
    Pattern{"perm",  {5},    {9}, true, 100, {3, 4, 5}}
  };
  std::vector<DisjointGroup> groups = {
    DisjointGroup{{0, 1}, true},  // 2 + 3 = 5
    DisjointGroup{{2}},           // 7
    DisjointGroup{{3, 4}, true}   // 1 + 4 = 5
  };
  StageHeuristic h(pdbs, groups, patterns, zz_alphabet());
  CubeState state = CubeState::solved();
  
  CHECK(h.groupIsSummed(0), "Group 0 summed: disjoint move supports + opt-in");
  CHECK(!h.groupIsSummed(1), "Group 1 not summed: no explicit opt-in");
  CHECK(h.groupIsSummed(2), "Group 2 summed: disjoint move supports + opt-in");
  CHECK(h(state) == 7, "Three groups: max(5, 7, 5) = 7");
}

void testEmptyPdbList() {
  StageHeuristic h({}, {});
  CubeState state = CubeState::solved();
  CHECK(h(state) == 0, "Empty PDB list returns 0");
}

void testSinglePdb() {
  std::vector<PdbLookup> pdbs = {makeMockPdb("pdb1", 4)};
  StageHeuristic h(pdbs, {});
  CubeState state = CubeState::solved();
  CHECK(h(state) == 4, "Single PDB returns its value");
}

void testSingleGroupMultiplePdbs() {
  // BEFORE: one group with all three PDBs was summed unconditionally and the
  //   test asserted h == 6 (= 2 + 3 + 1).
  // AFTER: the same arithmetic is exercised through the structural gate --
  //   the three constituents declare pairwise-disjoint move supports over
  //   {U, R, L} and the group is explicitly opted in, so the sum is used.
  //   The refusal case (no opt-in -> MAX) is checked right after.
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 2),
    makeMockPdb("pdb2", 3),
    makeMockPdb("pdb3", 1)
  };
  std::vector<Pattern> patterns = {
    Pattern{"left",  {0, 1}, {2}, true, 100, {12, 13, 14}},
    Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},
    Pattern{"coll",  {6},    {7}, true, 100, {0, 1, 2}}
  };
  std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1, 2}, true}};
  StageHeuristic h(pdbs, groups, patterns, zz_alphabet());
  CubeState state = CubeState::solved();
  
  CHECK(h.groupIsSummed(0), "Single group summed: disjoint move supports + opt-in");
  CHECK(h(state) == 6, "Single group with all PDBs: sum = 2+3+1=6");
  
  // Same group without the explicit opt-in -> MAX, never the sum.
  std::vector<DisjointGroup> not_opted_in = {DisjointGroup{{0, 1, 2}}};
  StageHeuristic h_refused(pdbs, not_opted_in, patterns, zz_alphabet());
  CHECK(!h_refused.groupIsSummed(0), "Same group without opt-in is refused");
  CHECK(h_refused(state) == 3, "Refused group uses MAX: max(2,3,1) = 3");
}

void testStageConfigValidation() {
  StageConfig config;
  config.stage_name = "test";
  config.patterns = {
    Pattern{"p1", {0}, {1}, true, 100},
    Pattern{"p2", {2}, {3}, true, 100}
  };
  config.disjoint_groups = {DisjointGroup{{0, 1}}};
  config.allowed_moves = {0, 1, 2};
  config.goal_predicate = [](const CubeState&) { return true; };
  config.goal_states = {CubeState::solved()};
  
  CHECK(config.validate(), "Valid config passes validation");
  
  // Invalid: overlapping pieces in disjoint group
  StageConfig bad_config = config;
  bad_config.patterns[1].corner_indices = {0};  // Overlaps with p1 corner 0
  CHECK(!bad_config.disjoint_groups[0].verify_disjoint(bad_config.patterns),
        "Overlapping pieces detected in disjoint group");
}

void testMakeStageHeuristic() {
  // BEFORE: the config declared a group with no opt-in and no declared move
  //   support, and the test asserted h == 7 (= 3 + 4).
  // AFTER: the config's patterns declare disjoint move supports over the
  //   stage's own move set and the group is explicitly opted in, so the sum
  //   is used and the arithmetic is unchanged.
  StageConfig config;
  config.stage_name = "test";
  config.patterns = {
    Pattern{"p1", {0}, {}, true, 100, {0, 1, 2}},
    Pattern{"p2", {1}, {}, true, 100, {3, 4, 5}}
  };
  config.disjoint_groups = {DisjointGroup{{0, 1}, true}};
  config.allowed_moves = {0, 1, 2, 3, 4, 5};
  config.goal_predicate = [](const CubeState&) { return true; };
  config.goal_states = {CubeState::solved()};
  
  std::vector<const std::uint8_t*> data_ptrs;
  std::vector<std::function<std::uint32_t(const CubeState&)>> index_fns;
  
  // Create mock data
  static std::vector<std::uint8_t> data1(50, 0), data2(50, 0);
  // Pack value 3 in data1, 4 in data2
  for (size_t i = 0; i < 100; ++i) {
    size_t bi = i >> 1;
    uint8_t v1 = 3, v2 = 4;
    if ((i & 1) == 0) {
      data1[bi] = (data1[bi] & 0xF0) | v1;
      data2[bi] = (data2[bi] & 0xF0) | v2;
    } else {
      data1[bi] = (data1[bi] & 0x0F) | (v1 << 4);
      data2[bi] = (data2[bi] & 0x0F) | (v2 << 4);
    }
  }
  data_ptrs = {data1.data(), data2.data()};
  index_fns = {
    [](const CubeState&) { return 0u; },
    [](const CubeState&) { return 0u; }
  };
  
  StageHeuristic h = makeStageHeuristic(config, data_ptrs, index_fns);
  CubeState state = CubeState::solved();
  
  // Disjoint group + opt-in -> sum = 3 + 4 = 7
  CHECK(config.group_may_be_summed(0), "Config reports the group may be summed");
  CHECK(h.groupIsSummed(0), "makeStageHeuristic: group summed");
  CHECK(h(state) == 7, "makeStageHeuristic: disjoint group sum = 7");
  CHECK(h.allValid(), "All PDBs valid");
}

void testMakeStageHeuristicWithoutOptInUsesMax() {
  // The refusal path through the factory: without the explicit opt-in the
  // very same config must fall back to MAX (4, not 7).
  StageConfig config;
  config.stage_name = "test";
  config.patterns = {
    Pattern{"p1", {0}, {}, true, 100, {0, 1, 2}},
    Pattern{"p2", {1}, {}, true, 100, {3, 4, 5}}
  };
  config.disjoint_groups = {DisjointGroup{{0, 1}}};  // no opt-in
  config.allowed_moves = {0, 1, 2, 3, 4, 5};
  config.goal_predicate = [](const CubeState&) { return true; };
  config.goal_states = {CubeState::solved()};

  static std::vector<std::uint8_t> data1(50, 0), data2(50, 0);
  for (size_t i = 0; i < 100; ++i) {
    size_t bi = i >> 1;
    uint8_t v1 = 3, v2 = 4;
    if ((i & 1) == 0) {
      data1[bi] = (data1[bi] & 0xF0) | v1;
      data2[bi] = (data2[bi] & 0xF0) | v2;
    } else {
      data1[bi] = (data1[bi] & 0x0F) | (v1 << 4);
      data2[bi] = (data2[bi] & 0x0F) | (v2 << 4);
    }
  }
  std::vector<const std::uint8_t*> data_ptrs = {data1.data(), data2.data()};
  std::vector<std::function<std::uint32_t(const CubeState&)>> index_fns = {
    [](const CubeState&) { return 0u; },
    [](const CubeState&) { return 0u; }
  };

  StageHeuristic h = makeStageHeuristic(config, data_ptrs, index_fns);
  CubeState state = CubeState::solved();

  CHECK(!config.group_may_be_summed(0), "Config reports the group may NOT be summed");
  CHECK(!h.groupIsSummed(0), "makeStageHeuristic: group refused");
  CHECK(h(state) == 4, "makeStageHeuristic without opt-in: MAX(3,4) = 4");
}

int main() {
  testMaxCombinatorNoGroups();
  testSumWithinGroupMaxAcrossGroups();
  testGroupMaxedWhenGateFails();
  testMultipleGroups();
  testEmptyPdbList();
  testSinglePdb();
  testSingleGroupMultiplePdbs();
  testStageConfigValidation();
  testMakeStageHeuristic();
  testMakeStageHeuristicWithoutOptInUsesMax();
  
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}
