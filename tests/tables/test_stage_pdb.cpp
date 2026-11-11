#include <vector>
#include <functional>
#include <cstdint>
#include <iostream>
#include <filesystem>
#include <string>

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
  // Instead, test the nibble packing logic directly -- but through the PRODUCTION
  // packer (`tables::packNibbleDistances`), not a local re-implementation, so that
  // the test actually exercises the shipped code.
  std::vector<std::uint8_t> dist = {0, 1, 2, 3, 4, kDbSentinel, kDbSentinel};
  std::size_t num_states = 7;

  std::vector<std::uint8_t> packed = rubik::tables::packNibbleDistances(dist);

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
  std::vector<std::uint8_t> packed5 = rubik::tables::packNibbleDistances(dist5);
  CHECK(packed5.size() == 3, "5 states -> 3 bytes");
  CHECK((packed5[0] & 0x0F) == 0, "packed5[0] low nibble = 0");
  CHECK((packed5[0] >> 4) == 1, "packed5[0] high nibble = 1");
  CHECK((packed5[1] & 0x0F) == 2, "packed5[1] low nibble = 2");
  CHECK((packed5[1] >> 4) == 3, "packed5[1] high nibble = 3");
  CHECK((packed5[2] & 0x0F) == 4, "packed5[2] low nibble = 4");
  CHECK((packed5[2] >> 4) == 0xF, "packed5[2] high nibble = sentinel (unused)");
  
  // Test even number of states
  std::vector<std::uint8_t> dist4 = {0, 1, 2, 3};
  std::vector<std::uint8_t> packed4 = rubik::tables::packNibbleDistances(dist4);
  CHECK(packed4.size() == 2, "4 states -> 2 bytes");
  CHECK(packed4[0] == 0x10, "packed4[0] = 0x10 (0,1)");
  CHECK(packed4[1] == 0x32, "packed4[1] = 0x32 (2,3)");
}

void testNibbleSentinelDecodesToZero() {
  // The nibble sentinel 0x0F means "not reached" and must decode to 0.
  const std::uint8_t packed[1] = {0xFF};
  CHECK(rubik::tables::unpackNibbleDistance(packed, 0) == 0,
        "Sentinel nibble (even entry) decodes to 0");
  CHECK(rubik::tables::unpackNibbleDistance(packed, 1) == 0,
        "Sentinel nibble (odd entry) decodes to 0");
  CHECK(rubik::tables::kNibbleUnreached == 0x0F,
        "Nibble sentinel is 0x0F");
  CHECK(rubik::tables::kNibbleMaxDistance == 14,
        "Nibble format's usable distance range ends at 14 (0x0F is the sentinel)");
}

void testNibblePackerDoesNotContaminateNeighbour() {
  // BUG-01 regression: a value with bit 4 set must never spill into the
  // neighbouring nibble.  The old packer OR-ed the raw byte, so 0xFE
  // overwrote the odd neighbour with 0x0F (the sentinel).
  std::vector<std::uint8_t> dist = {0xFE, 0x01, 0x02, 0x03};
  std::vector<std::uint8_t> packed = rubik::tables::packNibbleDistances(dist);
  CHECK(rubik::tables::unpackNibbleDistance(packed.data(), 0) == 0x0E,
        "Out-of-range even entry keeps only its low nibble");
  CHECK(rubik::tables::unpackNibbleDistance(packed.data(), 1) == 0x01,
        "Odd neighbour is NOT contaminated by the even entry");
  CHECK(rubik::tables::unpackNibbleDistance(packed.data(), 2) == 0x02,
        "Second byte's even entry is intact");
  CHECK(rubik::tables::unpackNibbleDistance(packed.data(), 3) == 0x03,
        "Second byte's odd entry is intact");
}

void testNibbleRangeGuardRefusesOverlongPattern() {
  // A pattern whose true diameter exceeds the nibble format's range must be
  // rejected loudly, never silently truncated to 14.
  // `predecessors(cur)` must yield the states that reach `cur` in one move
  // (the backward neighbours of the goal).
  const std::size_t kN = 30;
  std::vector<std::vector<std::uint32_t>> preds(kN);
  for (std::size_t i = 0; i + 1 < kN; ++i) {
    preds[i].push_back(static_cast<std::uint32_t>(i + 1));
  }
  const std::vector<std::uint32_t> goals = {0};

  bool threw = false;
  try {
    (void)rubik::tables::buildNibbleDistanceTable(
        kN, goals,
        [&](std::uint32_t idx) -> const std::vector<std::uint32_t>& {
          return preds[idx];
        });
  } catch (const rubik::tables::NibbleRangeExceeded&) {
    threw = true;
  }
  CHECK(threw, "buildNibbleDistanceTable throws when the diameter exceeds 14");

  // A diameter of exactly 14 is representable and must build exactly.
  const std::size_t kM = 15;
  std::vector<std::vector<std::uint32_t>> preds15(kM);
  for (std::size_t i = 0; i + 1 < kM; ++i) {
    preds15[i].push_back(static_cast<std::uint32_t>(i + 1));
  }
  auto packed = rubik::tables::buildNibbleDistanceTable(
      kM, goals,
      [&](std::uint32_t idx) -> const std::vector<std::uint32_t>& {
        return preds15[idx];
      });
  bool exact = true;
  for (std::size_t i = 0; i < kM; ++i) {
    if (rubik::tables::unpackNibbleDistance(packed.data(), i) !=
        static_cast<int>(i)) {
      exact = false;
    }
  }
  CHECK(exact, "A diameter of exactly 14 is stored exactly");
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
  testNibbleSentinelDecodesToZero();
  testNibblePackerDoesNotContaminateNeighbour();
  testNibbleRangeGuardRefusesOverlongPattern();
  testPdbFileIO();
  testDisjointGroupVerification();
  
  if (g_failures == 0) {
    std::cout << "ALL TESTS PASSED\n";
    return 0;
  }
  std::cerr << g_failures << " FAILURES\n";
  return 1;
}