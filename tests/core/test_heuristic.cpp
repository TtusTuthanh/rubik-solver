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
  // Two disjoint groups: {pdb1, pdb2} and {pdb3}
  // Group 0 sum: 3 + 4 = 7
  // Group 1 sum: 6
  // Max across groups: max(7, 6) = 7
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 3),
    makeMockPdb("pdb2", 4),
    makeMockPdb("pdb3", 6)
  };
  std::vector<DisjointGroup> groups = {
    DisjointGroup{{0, 1}},  // pdb1 + pdb2
    DisjointGroup{{2}}      // pdb3 alone
  };
  StageHeuristic h(pdbs, groups);
  CubeState state = CubeState::solved();
  
  CHECK(h(state) == 7, "Sum within group, max across: max(3+4, 6) = 7");
}

void testMultipleGroups() {
  // Three groups: {pdb1, pdb2}, {pdb3}, {pdb4, pdb5}
  // Group 0: 2 + 3 = 5
  // Group 1: 7
  // Group 2: 1 + 4 = 5
  // Max: max(5, 7, 5) = 7
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 2),
    makeMockPdb("pdb2", 3),
    makeMockPdb("pdb3", 7),
    makeMockPdb("pdb4", 1),
    makeMockPdb("pdb5", 4)
  };
  std::vector<DisjointGroup> groups = {
    DisjointGroup{{0, 1}},
    DisjointGroup{{2}},
    DisjointGroup{{3, 4}}
  };
  StageHeuristic h(pdbs, groups);
  CubeState state = CubeState::solved();
  
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
  // One group with all PDBs -> SUM of all
  std::vector<PdbLookup> pdbs = {
    makeMockPdb("pdb1", 2),
    makeMockPdb("pdb2", 3),
    makeMockPdb("pdb3", 1)
  };
  std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1, 2}}};
  StageHeuristic h(pdbs, groups);
  CubeState state = CubeState::solved();
  CHECK(h(state) == 6, "Single group with all PDBs: sum = 2+3+1=6");
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
  StageConfig config;
  config.stage_name = "test";
  config.patterns = {
    Pattern{"p1", {0}, {}, true, 100},
    Pattern{"p2", {1}, {}, true, 100}
  };
  config.disjoint_groups = {DisjointGroup{{0, 1}}};
  config.allowed_moves = {0, 1, 2};
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
  
  // Disjoint group -> sum = 3 + 4 = 7
  CHECK(h(state) == 7, "makeStageHeuristic: disjoint group sum = 7");
  CHECK(h.allValid(), "All PDBs valid");
}

int main() {
  testMaxCombinatorNoGroups();
  testSumWithinGroupMaxAcrossGroups();
  testMultipleGroups();
  testEmptyPdbList();
  testSinglePdb();
  testSingleGroupMultiplePdbs();
  testStageConfigValidation();
  testMakeStageHeuristic();
  
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}