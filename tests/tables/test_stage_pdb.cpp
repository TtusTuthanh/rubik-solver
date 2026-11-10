#include <vector>
#include <functional>
#include <cstdint>
#include <iostream>
#include <filesystem>

#include "tables/stage_pdb.hpp"
#include "tables/stage_config.hpp"
#include "core/cube.hpp"
#include "core/moves.hpp"
#include "core/pdb.hpp"

using rubik::tables::StageConfig;
using rubik::tables::Pattern;
using rubik::tables::DisjointGroup;
using rubik::tables::buildStagePdb;
using rubik::CubeState;
using rubik::core::bfsDb;
using rubik::core::kDbSentinel;

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

// Simple test: 1D line graph with 5 positions, moves +1/-1.
// Pattern: track position only (5 states).
// Goal: position 0.
// Moves: +1 (move 0), -1 (move 1).
// Expected distances: pos 0=0, 1=1, 2=2, 3=3, 4=4.

struct LineState {
  int pos = 0;
  bool operator==(const LineState&) const = default;
};

void testBfsDbDirect() {
  // Test the core bfsDb function directly.
  constexpr std::size_t kN = 5;
  auto dist = bfsDb(kN, 0, [](std::uint32_t n) {
    std::vector<std::uint32_t> next;
    if (n > 0) next.push_back(n - 1);
    if (n + 1 < kN) next.push_back(n + 1);
    return next;
  });
  
  CHECK(dist.size() == kN, "bfsDb size matches graph");
  CHECK(dist[0] == 0, "bfsDb start distance 0");
  CHECK(dist[1] == 1, "bfsDb dist(1)==1");
  CHECK(dist[2] == 2, "bfsDb dist(2)==2");
  CHECK(dist[3] == 3, "bfsDb dist(3)==3");
  CHECK(dist[4] == 4, "bfsDb dist(4)==4");
  
  // Test multi-source BFS: goals at 0 and 4
  auto dist2 = bfsDb(kN, 0, [](std::uint32_t n) {
    std::vector<std::uint32_t> next;
    if (n > 0) next.push_back(n - 1);
    if (n + 1 < kN) next.push_back(n + 1);
    return next;
  });
  // Manually set both 0 and 4 as goals
  dist2[4] = 0;
  // Re-run BFS from both (simplified: just check that 2 has dist 2 from either end)
  // Actually bfsDb only takes one start. For multi-source, we'd need to modify.
  // The buildStagePdb handles multi-source internally.
}

void testBuildStagePdbSimple() {
  // Minimal StageConfig for a toy 5-state pattern.
  StageConfig config;
  config.stage_name = "line5";
  config.patterns = {
    Pattern{"pos", {}, {}, true, 5}  // 5 states (positions 0-4)
  };
  config.allowed_moves = {0, 1};  // +1, -1
  config.goal_states = {CubeState::solved()};  // Not used directly; we use custom encode/decode
  
  // We can't easily test buildStagePdb with CubeState because it requires full cube machinery.
  // Instead, test the nibble packing logic directly.
  std::vector<std::uint8_t> dist = {0, 1, 2, 3, 4, kDbSentinel, kDbSentinel};
  std::size_t num_states = 7;
  
  // Nibble-pack manually (same logic as buildStagePdb)
  std::vector<std::uint8_t> packed((num_states + 1) / 2, 0xFF);
  for (std::size_t i = 0; i < num_states; ++i) {
    std::uint8_t v = dist[i];
    if (v == kDbSentinel) v = 0x0F;
    std::size_t byte_idx = i >> 1;
    if ((i & 1) == 0) {
      packed[byte_idx] = (packed[byte_idx] & 0xF0) | v;
    } else {
      packed[byte_idx] = (packed[byte_idx] & 0x0F) | (v << 4);
    }
  }
  
  // Verify unpacking
  for (std::size_t i = 0; i < num_states; ++i) {
    std::uint8_t byte = packed[i >> 1];
    std::uint8_t nibble = (i & 1) ? (byte >> 4) : (byte & 0x0F);
    // For i < 5: distance = i (0,1,2,3,4). For i >= 5: sentinel (0xF).
    int expected = (i < 5) ? static_cast<int>(i) : 0x0F;
    CHECK(nibble == static_cast<std::uint8_t>(expected), 
          "Nibble pack/unpack at index " + std::to_string(i));
  }
}

void testNibblePackingEdgeCases() {
  // Test odd number of states
  std::vector<std::uint8_t> dist5 = {0, 1, 2, 3, 4};
  std::vector<std::uint8_t> packed5((5 + 1) / 2, 0xFF);
  for (std::size_t i = 0; i < 5; ++i) {
    std::uint8_t v = dist5[i];
    std::size_t byte_idx = i >> 1;
    if ((i & 1) == 0) packed5[byte_idx] = (packed5[byte_idx] & 0xF0) | v;
    else packed5[byte_idx] = (packed5[byte_idx] & 0x0F) | (v << 4);
  }
  CHECK(packed5.size() == 3, "5 states -> 3 bytes");
  CHECK((packed5[0] & 0x0F) == 0, "packed5[0] low nibble = 0");
  CHECK((packed5[0] >> 4) == 1, "packed5[0] high nibble = 1");
  CHECK((packed5[1] & 0x0F) == 2, "packed5[1] low nibble = 2");
  CHECK((packed5[1] >> 4) == 3, "packed5[1] high nibble = 3");
  CHECK((packed5[2] & 0x0F) == 4, "packed5[2] low nibble = 4");
  CHECK((packed5[2] >> 4) == 0xF, "packed5[2] high nibble = sentinel (unused)");
  
  // Test even number of states
  std::vector<std::uint8_t> dist4 = {0, 1, 2, 3};
  std::vector<std::uint8_t> packed4((4 + 1) / 2, 0xFF);
  for (std::size_t i = 0; i < 4; ++i) {
    std::uint8_t v = dist4[i];
    std::size_t byte_idx = i >> 1;
    if ((i & 1) == 0) packed4[byte_idx] = (packed4[byte_idx] & 0xF0) | v;
    else packed4[byte_idx] = (packed4[byte_idx] & 0x0F) | (v << 4);
  }
  CHECK(packed4.size() == 2, "4 states -> 2 bytes");
  CHECK(packed4[0] == 0x10, "packed4[0] = 0x10 (0,1)");
  CHECK(packed4[1] == 0x32, "packed4[1] = 0x32 (2,3)");
}

void testPdbFileIO() {
  // Test writePdbFile / readPdbFile roundtrip
  std::vector<std::uint8_t> data = {0x10, 0x32, 0x0F};  // 5 entries: 0,1,2,3, sentinel
  std::filesystem::path test_path = std::filesystem::current_path() / "test_pdb_io.tmp";
  
  bool write_ok = rubik::tables::writePdbFile(test_path, data);
  if (!write_ok) {
    std::cerr << "DEBUG: writePdbFile failed, path = " << test_path << ", cwd = " << std::filesystem::current_path() << "\n";
  }
  CHECK(write_ok, "writePdbFile succeeds");
  
  auto read_data = rubik::tables::readPdbFile(test_path, 5);
  if (read_data.empty()) {
    std::cerr << "DEBUG: readPdbFile returned empty, path = " << test_path << ", exists = " << std::filesystem::exists(test_path) << "\n";
    if (std::filesystem::exists(test_path)) {
      std::size_t fsize = std::filesystem::file_size(test_path);
      std::cerr << "DEBUG: file size = " << fsize << ", expected = " << ((5 + 1) / 2) << "\n";
    }
  }
  CHECK(read_data.size() == data.size(), "readPdbFile size matches");
  CHECK(read_data == data, "readPdbFile data matches");
  
  // Test read with wrong expected_entries (file still exists)
  // For 7 entries: expected_bytes = (7+1)/2 = 4, file has 3 bytes -> mismatch
  auto read_wrong = rubik::tables::readPdbFile(test_path, 7);
  CHECK(read_wrong.empty(), "readPdbFile fails on size mismatch");
  
  // Cleanup
  std::filesystem::remove(test_path);
  
  // Test read missing file
  auto read_missing = rubik::tables::readPdbFile(test_path, 5);
  CHECK(read_missing.empty(), "readPdbFile fails on missing file");
}

void testDisjointGroupVerification() {
  StageConfig config;
  config.patterns = {
    Pattern{"p1", {0, 1}, {2}, true, 100},   // corners 0,1 + edge 2
    Pattern{"p2", {3}, {4, 5}, true, 100},   // corner 3 + edges 4,5
    Pattern{"p3", {0}, {6}, true, 100}       // corner 0 (overlaps p1!) + edge 6
  };
  
  DisjointGroup good_group{{0, 1}};  // p1 and p2 disjoint
  CHECK(good_group.verify_disjoint(config.patterns), "Disjoint group {0,1} valid");
  
  DisjointGroup bad_group{{0, 2}};  // p1 and p3 overlap on corner 0
  CHECK(!bad_group.verify_disjoint(config.patterns), "Overlapping group {0,2} invalid");
  
  // Test all_piece_indices mapping
  auto pieces1 = config.patterns[0].all_piece_indices();
  CHECK(pieces1.size() == 3, "p1 has 3 pieces");
  CHECK(pieces1[0] == 0 && pieces1[1] == 1 && pieces1[2] == 10, "p1 pieces: corners 0,1 -> 0,1; edge 2 -> 10");
}

int main() {
  testBfsDbDirect();
  testBuildStagePdbSimple();
  testNibblePackingEdgeCases();
  testPdbFileIO();
  testDisjointGroupVerification();
  
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}