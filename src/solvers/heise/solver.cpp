#include "solvers/heise/solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include "core/moves.hpp"

namespace rubik::heise {

// ---------------------------------------------------------------------------
// Corners 0..7, edges 0..11 as in core/cube.hpp.
// Squares: D edges 4,5,6,7; D corners 4,5,6 solved, DRB(7) oriented.
// F2L-1: + FR(8),FL(9),BL(10) + U edges 0..3 oriented.
// Pairs: + BR(11), U edges 0..3 solved, DRB(7), URF(0) solved.
// Final: solved (UFL,ULB,UBR remain before this stage).
// ---------------------------------------------------------------------------

bool is_squares_solved(const CubeState& state) {
  for (int i = 4; i < 8; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (state.ep[ii] != static_cast<std::uint8_t>(i)) return false;
    if (state.eo[ii] != 0u) return false;
  }
  for (int i = 4; i < 8; ++i) {
    if (state.co[static_cast<std::size_t>(i)] != 0u) return false;
  }
  return true;
}

bool is_square_solved(const CubeState& state) { return is_squares_solved(state); }

bool is_heise_blocks_solved(const CubeState& state) {
  return is_squares_solved(state);
}

bool is_f2l_minus_1_solved(const CubeState& state) {
  if (!is_squares_solved(state)) return false;
  for (int i = 8; i < 11; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (state.ep[ii] != static_cast<std::uint8_t>(i)) return false;
    if (state.eo[ii] != 0u) return false;
  }
  for (int i = 0; i < 4; ++i) {
    if (state.eo[static_cast<std::size_t>(i)] != 0u) return false;
  }
  return true;
}

bool is_f2l1_solved(const CubeState& state) { return is_f2l_minus_1_solved(state); }

bool is_f2l_1_solved(const CubeState& state) {
  return is_f2l_minus_1_solved(state);
}

bool is_corner_edge_pairs_solved(const CubeState& state) {
  if (!is_f2l_minus_1_solved(state)) return false;
  if (state.ep[11] != 11u || state.eo[11] != 0u) return false;  // BR
  for (int i = 0; i < 4; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (state.ep[ii] != static_cast<std::uint8_t>(i)) return false;
    if (state.eo[ii] != 0u) return false;
  }
  if (state.cp[7] != 7u || state.co[7] != 0u) return false;  // DRB
  if (state.cp[0] != 0u || state.co[0] != 0u) return false;  // URF
  return true;
}

bool is_pairs_solved(const CubeState& state) {
  return is_corner_edge_pairs_solved(state);
}

bool is_last_edges_solved(const CubeState& state) {
  return is_corner_edge_pairs_solved(state);
}

bool is_final_corners_solved(const CubeState& state) { return is_solved(state); }

std::vector<std::string> stage_names() {
  return {"squares", "f2l_minus_1", "corner_edge_pairs", "final_corners"};
}

std::vector<std::string> squares_allowed_moves() {
  std::vector<std::string> out;
  for (const Move& m : all_face_moves()) out.push_back(m.token);
  return out;
}

std::vector<std::string> f2l_allowed_moves() { return squares_allowed_moves(); }

std::vector<std::string> corner_edge_pairs_allowed_moves() {
  return squares_allowed_moves();
}

std::vector<std::string> final_corners_allowed_moves() {
  return squares_allowed_moves();
}

std::vector<std::string> allowed_moves_for_stage(const std::string& stage_name) {
  if (stage_name == "squares" || stage_name == "squares_solved") {
    return squares_allowed_moves();
  }
  if (stage_name == "f2l_minus_1" || stage_name == "f2l1" ||
      stage_name == "f2l_1") {
    return f2l_allowed_moves();
  }
  if (stage_name == "corner_edge_pairs" || stage_name == "pairs") {
    return corner_edge_pairs_allowed_moves();
  }
  if (stage_name == "final_corners" || stage_name == "final") {
    return final_corners_allowed_moves();
  }
  return {};
}

namespace {

// ---------------------------------------------------------------------------
// Ranking helpers (factorial number system) for pattern indices.
// ---------------------------------------------------------------------------
std::uint32_t rankPerm(const std::array<std::uint8_t, 6>& pos, int n, int k,
                       const std::array<std::uint32_t, 6>& blocks) {
  std::uint32_t rank = 0;
  bool used[12] = {};
  for (int d = 0; d < k; ++d) {
    const int p = pos[static_cast<std::size_t>(d)];
    int cnt = 0;
    for (int v = 0; v < p; ++v) {
      if (!used[static_cast<std::size_t>(v)]) ++cnt;
    }
    rank += static_cast<std::uint32_t>(cnt) *
            blocks[static_cast<std::size_t>(d)];
    used[static_cast<std::size_t>(p)] = true;
  }
  (void)n;
  return rank;
}

void unrankPerm(std::uint32_t rank, int n, int k,
                const std::array<std::uint32_t, 6>& blocks,
                std::array<std::uint8_t, 6>& pos) {
  bool used[12] = {};
  for (int d = 0; d < k; ++d) {
    const std::uint32_t block = blocks[static_cast<std::size_t>(d)];
    const int kk = static_cast<int>(rank / block);
    rank %= block;
    int seen = -1;
    int val = -1;
    for (int v = 0; v < n; ++v) {
      if (!used[static_cast<std::size_t>(v)]) {
        ++seen;
        if (seen == kk) {
          val = v;
          break;
        }
      }
    }
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(val);
    used[static_cast<std::size_t>(val)] = true;
  }
}

std::uint32_t cornerOrientIdx(const std::array<std::uint8_t, 6>& ori, int k) {
  std::uint32_t idx = 0;
  std::uint32_t mult = 1;
  for (int d = 0; d < k; ++d) {
    idx += static_cast<std::uint32_t>(ori[static_cast<std::size_t>(d)]) * mult;
    mult *= 3u;
  }
  return idx;
}

void cornerOrientUnrank(std::uint32_t idx, int k,
                        std::array<std::uint8_t, 6>& ori) {
  for (int d = 0; d < k; ++d) {
    ori[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(idx % 3u);
    idx /= 3u;
  }
}

std::uint32_t edgeOrientBits(const std::array<std::uint8_t, 6>& ori, int k) {
  std::uint32_t bits = 0;
  for (int d = 0; d < k; ++d) {
    if (ori[static_cast<std::size_t>(d)] != 0u) {
      bits |= (1u << static_cast<std::uint32_t>(d));
    }
  }
  return bits;
}

void edgeOrientUnrank(std::uint32_t bits, int k,
                      std::array<std::uint8_t, 6>& ori) {
  for (int d = 0; d < k; ++d) {
    ori[static_cast<std::size_t>(d)] =
        static_cast<std::uint8_t>((bits >> static_cast<std::uint32_t>(d)) & 1u);
  }
}

// Blocks for rankPerm: P(n-d-1, k-d-1) per depth.
constexpr std::array<std::uint32_t, 6> kCorner2Blocks = {7, 1, 0, 0, 0, 0};
constexpr std::array<std::uint32_t, 6> kCorner3Blocks = {42, 6, 1, 0, 0, 0};
constexpr std::array<std::uint32_t, 6> kEdge3Blocks = {110, 10, 1, 0, 0, 0};
constexpr std::array<std::uint32_t, 6> kEdge4Blocks = {990, 90, 9, 1, 0, 0};
constexpr std::uint32_t kCorner2Rank = 56u;
constexpr std::uint32_t kCorner3Rank = 336u;
constexpr std::uint32_t kEdge3Rank = 1320u;
constexpr std::uint32_t kEdge4Rank = 11880u;
constexpr std::uint32_t kCorner2Entries = 504u;  // 56*9
constexpr std::uint32_t kCorner3Entries = 9072u;  // 336*27
constexpr std::uint32_t kEdge3Entries = 10560u;  // 1320*8
constexpr std::uint32_t kEdge4Entries = 190080u;  // 11880*16
constexpr std::uint32_t kEoEntries = 2048u;  // 2^11
constexpr std::uint32_t kBrEntries = 24u;  // 12*2
// Permutation-only blocks/ranks for combined stage 3/4 PDBs.
constexpr std::array<std::uint32_t, 6> kEdge5PermBlocks = {7920, 720, 72, 8, 1, 0};
constexpr std::array<std::uint32_t, 6> kEdge4PermBlocks = {990, 90, 9, 1, 0, 0};
constexpr std::array<std::uint32_t, 6> kCorner2PermBlocks = {7, 1, 0, 0, 0, 0};
constexpr std::array<std::uint32_t, 6> kCorner3PermBlocks = {42, 6, 1, 0, 0, 0};
constexpr std::array<std::uint32_t, 6> kCorner4PermBlocks = {210, 30, 5, 1, 0, 0};
constexpr std::uint32_t kEdge5PermRank = 95040u;   // P(12,5)
constexpr std::uint32_t kEdge4PermRank = 11880u;   // P(12,4)
constexpr std::uint32_t kCorner2PermRank = 56u;    // P(8,2)
constexpr std::uint32_t kCorner3PermRank = 336u;   // P(8,3)

// Cubie lists in pattern order.
constexpr std::array<int, 4> kDEdges = {4, 5, 6, 7};
constexpr std::array<int, 3> kDCorners3 = {4, 5, 6};
constexpr std::array<int, 3> kMidEdges = {8, 9, 10};
constexpr std::array<int, 4> kUEdges = {0, 1, 2, 3};
constexpr std::array<int, 2> kCornerPair = {7, 0};  // DRB + URF
constexpr std::array<int, 3> kFinalCorners = {1, 2, 3};  // UFL,ULB,UBR

// ---------------------------------------------------------------------------
// Move data derived from the frozen core (verified against rubik::apply in
// tests). Indices 0..17 follow all_face_moves() order.
// ---------------------------------------------------------------------------
struct MoveData {
  std::array<std::array<std::uint8_t, 8>, 18> cmCp{};
  std::array<std::array<std::uint8_t, 8>, 18> cmCo{};
  std::array<std::array<std::uint8_t, 12>, 18> cmEp{};
  std::array<std::array<std::uint8_t, 12>, 18> cmEo{};
  std::array<std::array<std::uint8_t, 8>, 18> destC{};
  std::array<std::array<std::uint8_t, 8>, 18> addC{};
  std::array<std::array<std::uint8_t, 12>, 18> destE{};
  std::array<std::array<std::uint8_t, 12>, 18> addE{};
  std::array<std::string, 18> tokens{};
  std::array<int, 18> faceGroup{};
};

MoveData buildMoveData() {
  MoveData md;
  std::vector<std::string> toks;
  for (const Move& m : all_face_moves()) toks.push_back(m.token);
  const CubeState solvedState = CubeState::solved();
  for (std::size_t i = 0; i < 18u; ++i) {
    const CubeState after = apply(solvedState, Move{toks[i]});
    md.tokens[i] = toks[i];
    md.faceGroup[i] = static_cast<int>(i / 3u);
    for (std::size_t p = 0; p < 8u; ++p) {
      md.cmCp[i][p] = after.cp[p];
      md.cmCo[i][p] = after.co[p];
    }
    for (std::size_t p = 0; p < 12u; ++p) {
      md.cmEp[i][p] = after.ep[p];
      md.cmEo[i][p] = after.eo[p];
    }
    for (int p = 0; p < 8; ++p) {
      const std::size_t pp = static_cast<std::size_t>(p);
      const std::uint8_t oldPos = after.cp[pp];
      md.destC[i][oldPos] = static_cast<std::uint8_t>(p);
      md.addC[i][pp] = after.co[pp];
    }
    for (int p = 0; p < 12; ++p) {
      const std::size_t pp = static_cast<std::size_t>(p);
      const std::uint8_t oldPos = after.ep[pp];
      md.destE[i][oldPos] = static_cast<std::uint8_t>(p);
      md.addE[i][pp] = after.eo[pp];
    }
  }
  return md;
}

std::mutex gMoveMutex;
std::optional<MoveData> gMoveData;

const MoveData& getMoveData() {
  std::lock_guard<std::mutex> lock(gMoveMutex);
  if (!gMoveData.has_value()) gMoveData = buildMoveData();
  return *gMoveData;
}

inline CubeState fastApply(const CubeState& s, int moveIdx,
                           const MoveData& md) {
  CubeState n = s;
  const std::size_t mi = static_cast<std::size_t>(moveIdx);
  for (std::size_t p = 0; p < 8u; ++p) {
    const std::uint8_t src = md.cmCp[mi][p];
    n.cp[p] = s.cp[src];
    n.co[p] = static_cast<std::uint8_t>(
        (static_cast<unsigned>(s.co[src]) + static_cast<unsigned>(md.cmCo[mi][p])) % 3u);
  }
  for (std::size_t p = 0; p < 12u; ++p) {
    const std::uint8_t src = md.cmEp[mi][p];
    n.ep[p] = s.ep[src];
    n.eo[p] = static_cast<std::uint8_t>(s.eo[src] ^ md.cmEo[mi][p]);
  }
  return n;
}

// ---------------------------------------------------------------------------
// Pattern indices from a cubie state.
// ---------------------------------------------------------------------------
std::uint32_t eoIdx(const CubeState& s) {
  std::uint32_t bits = 0;
  for (int i = 0; i < 11; ++i) {
    if (s.eo[static_cast<std::size_t>(i)] != 0u) {
      bits |= (1u << static_cast<std::uint32_t>(i));
    }
  }
  return bits;
}

// ---------------------------------------------------------------------------
// Pattern move tables and PDB BFS.
// ---------------------------------------------------------------------------
std::vector<int> faceMoveIndices() {
  std::vector<int> out;
  for (int i = 0; i < 18; ++i) out.push_back(i);
  return out;
}

std::vector<std::uint32_t> buildCornerMoveTable(
    int k, const std::array<std::uint32_t, 6>& blocks, std::uint32_t rankEntries,
    std::uint32_t orientEntries, const std::vector<int>& moves,
    const MoveData& md) {
  const std::uint32_t entries = rankEntries * orientEntries;
  std::vector<std::uint32_t> table(static_cast<std::size_t>(entries) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < entries; ++idx) {
    std::array<std::uint8_t, 6> pos{};
    std::array<std::uint8_t, 6> ori{};
    unrankPerm(idx / orientEntries, 8, k, blocks, pos);
    cornerOrientUnrank(idx % orientEntries, k, ori);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const int mv = moves[mi];
      const std::size_t mmi = static_cast<std::size_t>(mv);
      std::array<std::uint8_t, 6> npos{};
      std::array<std::uint8_t, 6> nori{};
      for (int d = 0; d < k; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        const std::uint8_t np = md.destC[mmi][pos[dd]];
        npos[dd] = np;
        nori[dd] = static_cast<std::uint8_t>(
            (static_cast<unsigned>(ori[dd]) + static_cast<unsigned>(md.addC[mmi][np])) % 3u);
      }
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          rankPerm(npos, 8, k, blocks) * orientEntries + cornerOrientIdx(nori, k);
    }
  }
  return table;
}

std::vector<std::uint32_t> buildEdgeMoveTable(
    int k, const std::array<std::uint32_t, 6>& blocks, std::uint32_t rankEntries,
    std::uint32_t orientEntries, const std::vector<int>& moves,
    const MoveData& md) {
  const std::uint32_t entries = rankEntries * orientEntries;
  std::vector<std::uint32_t> table(static_cast<std::size_t>(entries) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < entries; ++idx) {
    std::array<std::uint8_t, 6> pos{};
    std::array<std::uint8_t, 6> ori{};
    unrankPerm(idx / orientEntries, 12, k, blocks, pos);
    edgeOrientUnrank(idx % orientEntries, k, ori);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const int mv = moves[mi];
      const std::size_t mmi = static_cast<std::size_t>(mv);
      std::array<std::uint8_t, 6> npos{};
      std::array<std::uint8_t, 6> nori{};
      for (int d = 0; d < k; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        const std::uint8_t np = md.destE[mmi][pos[dd]];
        npos[dd] = np;
        nori[dd] = static_cast<std::uint8_t>(ori[dd] ^ md.addE[mmi][np]);
      }
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          rankPerm(npos, 12, k, blocks) * orientEntries + edgeOrientBits(nori, k);
    }
  }
  return table;
}

std::vector<std::uint8_t> buildSingleDb(
    const std::vector<std::uint32_t>& table, std::uint32_t entries,
    std::uint32_t startIdx, std::size_t moveCount) {
  std::vector<std::uint8_t> dist(entries, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(entries);
  dist[startIdx] = 0u;
  queue.push_back(startIdx);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    for (std::size_t mi = 0; mi < moveCount; ++mi) {
      const std::uint32_t nidx = table[static_cast<std::size_t>(idx) * moveCount + mi];
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

// Permutation-only move tables (successors ignore orientation) and combined
// BFS. Used for the stage 3/4 PDBs where joint edge+corner permutation is
// the pruning-relevant pattern.
std::vector<std::uint32_t> buildEdgePermTable(
    int k, const std::array<std::uint32_t, 6>& blocks,
    std::uint32_t rankEntries, const std::vector<int>& moves,
    const MoveData& md) {
  std::vector<std::uint32_t> table(
      static_cast<std::size_t>(rankEntries) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < rankEntries; ++idx) {
    std::array<std::uint8_t, 6> pos{};
    unrankPerm(idx, 12, k, blocks, pos);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const std::size_t mmi = static_cast<std::size_t>(moves[mi]);
      std::array<std::uint8_t, 6> npos{};
      for (int d = 0; d < k; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        npos[dd] = md.destE[mmi][pos[dd]];
      }
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          rankPerm(npos, 12, k, blocks);
    }
  }
  return table;
}

std::vector<std::uint32_t> buildCornerPermTable(
    int k, const std::array<std::uint32_t, 6>& blocks,
    std::uint32_t rankEntries, const std::vector<int>& moves,
    const MoveData& md) {
  std::vector<std::uint32_t> table(
      static_cast<std::size_t>(rankEntries) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < rankEntries; ++idx) {
    std::array<std::uint8_t, 6> pos{};
    unrankPerm(idx, 8, k, blocks, pos);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const std::size_t mmi = static_cast<std::size_t>(moves[mi]);
      std::array<std::uint8_t, 6> npos{};
      for (int d = 0; d < k; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        npos[dd] = md.destC[mmi][pos[dd]];
      }
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          rankPerm(npos, 8, k, blocks);
    }
  }
  return table;
}

std::vector<std::uint8_t> buildCombinedPermDb(
    const std::vector<std::uint32_t>& edgeTable,
    const std::vector<std::uint32_t>& cornerTable,
    std::uint32_t edgeEntries, std::uint32_t cornerEntries,
    std::uint32_t startIdx, std::size_t moveCount) {
  const std::uint32_t total = edgeEntries * cornerEntries;
  std::vector<std::uint8_t> dist(total, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(total);
  dist[startIdx] = 0u;
  queue.push_back(startIdx);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    const std::uint32_t eIdx = idx / cornerEntries;
    const std::uint32_t cIdx = idx % cornerEntries;
    for (std::size_t mi = 0; mi < moveCount; ++mi) {
      const std::uint32_t ne =
          edgeTable[static_cast<std::size_t>(eIdx) * moveCount + mi];
      const std::uint32_t nc =
          cornerTable[static_cast<std::size_t>(cIdx) * moveCount + mi];
      const std::uint32_t nidx = ne * cornerEntries + nc;
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

std::uint32_t cornerOriIndex(const std::array<std::uint8_t, 8>& co) {
  std::uint32_t idx = 0;
  std::uint32_t mult = 1;
  for (int i = 0; i < 7; ++i) {
    idx += static_cast<std::uint32_t>(co[static_cast<std::size_t>(i)]) * mult;
    mult *= 3u;
  }
  return idx;
}
void cornerOriUnrank(std::uint32_t idx, std::array<std::uint8_t, 8>& co) {
  int sum = 0;
  for (int i = 0; i < 7; ++i) {
    co[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(idx % 3u);
    idx /= 3u;
    sum += co[static_cast<std::size_t>(i)];
  }
  co[7] = static_cast<std::uint8_t>((3 - (sum % 3)) % 3);
}
std::vector<std::uint8_t> buildDOrientDb(const MoveData& md) {
  constexpr std::uint32_t N = 2187u;
  std::vector<std::uint32_t> table(18u * N, 0u);
  for (int m = 0; m < 18; ++m) {
    for (std::uint32_t r = 0; r < N; ++r) {
      std::array<std::uint8_t, 8> co{};
      cornerOriUnrank(r, co);
      std::array<std::uint8_t, 8> nco{};
      for (int p = 0; p < 8; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        const std::uint8_t src = md.cmCp[static_cast<std::size_t>(m)][pp];
        nco[pp] = static_cast<std::uint8_t>(
            (static_cast<unsigned>(co[src]) +
             static_cast<unsigned>(md.cmCo[static_cast<std::size_t>(m)][pp])) % 3u);
      }
      table[static_cast<std::size_t>(m) * N + r] = cornerOriIndex(nco);
    }
  }
  std::vector<std::uint8_t> dist(N, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(N);
  for (std::uint32_t r = 0; r < N; ++r) {
    std::array<std::uint8_t, 8> co{};
    cornerOriUnrank(r, co);
    if (co[4] == 0u && co[5] == 0u && co[6] == 0u && co[7] == 0u) {
      dist[r] = 0u;
      queue.push_back(r);
    }
  }
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    for (int m = 0; m < 18; ++m) {
      const std::uint32_t succ = table[static_cast<std::size_t>(m) * N + idx];
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildOriFullDb(const MoveData& md) {
  constexpr std::uint32_t N = 2187u;
  std::vector<std::uint32_t> table(18u * N, 0u);
  for (int m = 0; m < 18; ++m) {
    for (std::uint32_t r = 0; r < N; ++r) {
      std::array<std::uint8_t, 8> co{};
      cornerOriUnrank(r, co);
      std::array<std::uint8_t, 8> nco{};
      for (int p = 0; p < 8; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        const std::uint8_t src = md.cmCp[static_cast<std::size_t>(m)][pp];
        nco[pp] = static_cast<std::uint8_t>(
            (static_cast<unsigned>(co[src]) +
             static_cast<unsigned>(md.cmCo[static_cast<std::size_t>(m)][pp])) % 3u);
      }
      table[static_cast<std::size_t>(m) * N + r] = cornerOriIndex(nco);
    }
  }
  std::vector<std::uint8_t> dist(N, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(N);
  dist[0] = 0u;
  queue.push_back(0u);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    for (int m = 0; m < 18; ++m) {
      const std::uint32_t succ = table[static_cast<std::size_t>(m) * N + idx];
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildOri5Db(const MoveData& md) {
  constexpr std::uint32_t N = 2187u;
  std::vector<std::uint32_t> table(18u * N, 0u);
  for (int m = 0; m < 18; ++m) {
    for (std::uint32_t r = 0; r < N; ++r) {
      std::array<std::uint8_t, 8> co{};
      cornerOriUnrank(r, co);
      std::array<std::uint8_t, 8> nco{};
      for (int p = 0; p < 8; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        const std::uint8_t src = md.cmCp[static_cast<std::size_t>(m)][pp];
        nco[pp] = static_cast<std::uint8_t>(
            (static_cast<unsigned>(co[src]) +
             static_cast<unsigned>(md.cmCo[static_cast<std::size_t>(m)][pp])) % 3u);
      }
      table[static_cast<std::size_t>(m) * N + r] = cornerOriIndex(nco);
    }
  }
  std::vector<std::uint8_t> dist(N, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(N);
  for (std::uint32_t r = 0; r < N; ++r) {
    std::array<std::uint8_t, 8> co{};
    cornerOriUnrank(r, co);
    if (co[0] == 0u && co[4] == 0u && co[5] == 0u && co[6] == 0u &&
        co[7] == 0u) {
      dist[r] = 0u;
      queue.push_back(r);
    }
  }
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    for (int m = 0; m < 18; ++m) {
      const std::uint32_t succ = table[static_cast<std::size_t>(m) * N + idx];
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

// Per-position edge orientation PDB (2^11). Closed on EO bits alone.
void edgeOriUnrank(std::uint32_t bits, std::array<std::uint8_t, 12>& eo) {
  int sum = 0;
  for (int i = 0; i < 11; ++i) {
    eo[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>((bits >> static_cast<std::uint32_t>(i)) & 1u);
    sum += eo[static_cast<std::size_t>(i)];
  }
  eo[11] = static_cast<std::uint8_t>(sum % 2);
}
std::uint32_t edgeOriIndex(const std::array<std::uint8_t, 12>& eo) {
  std::uint32_t bits = 0;
  for (int i = 0; i < 11; ++i) {
    if (eo[static_cast<std::size_t>(i)] != 0u) bits |= (1u << static_cast<std::uint32_t>(i));
  }
  return bits;
}
std::vector<std::uint8_t> buildEoDb(const MoveData& md) {
  constexpr std::uint32_t N = 2048u;
  std::vector<std::uint32_t> table(18u * N, 0u);
  for (int m = 0; m < 18; ++m) {
    for (std::uint32_t r = 0; r < N; ++r) {
      std::array<std::uint8_t, 12> eo{};
      edgeOriUnrank(r, eo);
      std::array<std::uint8_t, 12> neo{};
      for (int p = 0; p < 12; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        const std::uint8_t src = md.cmEp[static_cast<std::size_t>(m)][pp];
        neo[pp] = static_cast<std::uint8_t>(eo[src] ^ md.cmEo[static_cast<std::size_t>(m)][pp]);
      }
      table[static_cast<std::size_t>(m) * N + r] = edgeOriIndex(neo);
    }
  }
  // BFS over 2^11 states; successor table is (move, idx) ordered.
  std::vector<std::uint8_t> dist(N, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(N);
  dist[0] = 0u;
  queue.push_back(0u);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    for (int m = 0; m < 18; ++m) {
      const std::uint32_t succ = table[static_cast<std::size_t>(m) * N + idx];
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildPairDb(int cornerCubie, int edgeCubie,
                                      const MoveData& md) {
  std::vector<std::uint8_t> dist(576u, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(576u);
  const std::uint32_t start = static_cast<std::uint32_t>(
      ((cornerCubie * 3 + 0) * 24 + (edgeCubie * 2 + 0)));
  dist[start] = 0u;
  queue.push_back(start);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    const std::uint32_t edgePart = idx % 24u;
    const std::uint32_t cornerPart = idx / 24u;
    const std::uint8_t cPos = static_cast<std::uint8_t>(cornerPart / 3u);
    const std::uint8_t cOri = static_cast<std::uint8_t>(cornerPart % 3u);
    const std::uint8_t ePos = static_cast<std::uint8_t>(edgePart / 2u);
    const std::uint8_t eOri = static_cast<std::uint8_t>(edgePart % 2u);
    for (int mv = 0; mv < 18; ++mv) {
      const auto& destC = md.destC[static_cast<std::size_t>(mv)];
      const auto& addC = md.addC[static_cast<std::size_t>(mv)];
      const auto& destE = md.destE[static_cast<std::size_t>(mv)];
      const auto& addE = md.addE[static_cast<std::size_t>(mv)];
      const std::uint8_t ncPos = destC[cPos];
      const std::uint8_t ncOri = static_cast<std::uint8_t>(
          (static_cast<unsigned>(cOri) + static_cast<unsigned>(addC[ncPos])) % 3u);
      const std::uint8_t nePos = destE[ePos];
      const std::uint8_t neOri = static_cast<std::uint8_t>(eOri ^ addE[nePos]);
      const std::uint32_t nidx = static_cast<std::uint32_t>(
          ((static_cast<unsigned>(ncPos) * 3u + static_cast<unsigned>(ncOri)) * 24u) +
          (static_cast<unsigned>(nePos) * 2u + static_cast<unsigned>(neOri)));
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildUrfDb(const MoveData& md) {
  std::vector<std::uint8_t> dist(24u, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(24u);
  dist[0] = 0u;
  queue.push_back(0u);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    const std::uint32_t pos = idx / 3u;
    const std::uint32_t ori = idx % 3u;
    for (int mv = 0; mv < 18; ++mv) {
      const std::size_t mmi = static_cast<std::size_t>(mv);
      const std::uint32_t npos = md.destC[mmi][pos];
      const std::uint32_t nori =
          (ori + md.addC[mmi][npos]) % 3u;
      const std::uint32_t nidx = npos * 3u + nori;
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildBrDb(const MoveData& md) {
  std::vector<std::uint8_t> dist(kBrEntries, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(kBrEntries);
  const std::uint32_t start = 11u * 2u;  // pos 11, ori 0
  dist[start] = 0u;
  queue.push_back(start);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    const std::uint32_t pos = idx / 2u;
    const std::uint32_t ori = idx % 2u;
    for (int mv = 0; mv < 18; ++mv) {
      const std::size_t mmi = static_cast<std::size_t>(mv);
      const std::uint32_t npos = md.destE[mmi][pos];
      const std::uint32_t nori = ori ^ md.addE[mmi][npos];
      const std::uint32_t nidx = npos * 2u + nori;
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

struct Pdbs {
  std::vector<std::uint8_t> dEdge;
  std::vector<std::uint8_t> dCorner3;
  std::vector<std::uint8_t> midEdge;
  std::vector<std::uint8_t> uEdge;
  std::vector<std::uint8_t> cornerPair;
  std::vector<std::uint8_t> finalCorner3;
  std::vector<std::uint8_t> br;
  std::vector<std::uint8_t> eo;
  std::vector<std::uint8_t> pairPerm;
  std::vector<std::uint8_t> finalPerm;
  std::array<std::vector<std::uint8_t>, 4> f2lPair;
  std::vector<std::uint8_t> urf;
  std::vector<std::uint8_t> dOrient;
  std::vector<std::uint8_t> uEdgeCorner;
  std::vector<std::uint8_t> oriFull;
  std::vector<std::uint8_t> ori5;
};

Pdbs buildPdbs(const MoveData& md) {
  Pdbs p;
  const std::vector<int> moves = faceMoveIndices();
  auto report = [](const std::vector<std::uint8_t>& db, const char* name) {
    std::size_t vis = 0;
    int mx = 0;
    for (std::uint8_t v : db) {
      if (v != 255u) ++vis;
      if (v != 255u && static_cast<int>(v) > mx) mx = static_cast<int>(v);
    }
    std::cout << std::format("heise pdb {}: {}/{} visited max {}\n", name, vis,
                             db.size(), mx);
  };
  // D edges (4 edges).
  {
    auto t = buildEdgeMoveTable(4, kEdge4Blocks, kEdge4Rank, 16u, moves, md);
    std::array<std::uint8_t, 6> pos{};
    for (int d = 0; d < 4; ++d) {
      pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(kDEdges[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t start = rankPerm(pos, 12, 4, kEdge4Blocks) * 16u;
    p.dEdge = buildSingleDb(t, kEdge4Entries, start, moves.size());
    report(p.dEdge, "dEdge");
  }
  // D corners (3 corners).
  {
    auto t = buildCornerMoveTable(3, kCorner3Blocks, kCorner3Rank, 27u, moves, md);
    std::array<std::uint8_t, 6> pos{};
    for (int d = 0; d < 3; ++d) {
      pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(kDCorners3[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t start = rankPerm(pos, 8, 3, kCorner3Blocks) * 27u;
    p.dCorner3 = buildSingleDb(t, kCorner3Entries, start, moves.size());
    report(p.dCorner3, "dCorner3");
  }
  // Middle edges (3 edges).
  {
    auto t = buildEdgeMoveTable(3, kEdge3Blocks, kEdge3Rank, 8u, moves, md);
    std::array<std::uint8_t, 6> pos{};
    for (int d = 0; d < 3; ++d) {
      pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(kMidEdges[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t start = rankPerm(pos, 12, 3, kEdge3Blocks) * 8u;
    p.midEdge = buildSingleDb(t, kEdge3Entries, start, moves.size());
    report(p.midEdge, "midEdge");
  }
  // U edges (4 edges).
  {
    auto t = buildEdgeMoveTable(4, kEdge4Blocks, kEdge4Rank, 16u, moves, md);
    std::array<std::uint8_t, 6> pos{};
    for (int d = 0; d < 4; ++d) {
      pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(kUEdges[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t start = rankPerm(pos, 12, 4, kEdge4Blocks) * 16u;
    p.uEdge = buildSingleDb(t, kEdge4Entries, start, moves.size());
    report(p.uEdge, "uEdge");
  }
  // Corner pair DRB+URF (2 corners).
  {
    auto t = buildCornerMoveTable(2, kCorner2Blocks, kCorner2Rank, 9u, moves, md);
    std::array<std::uint8_t, 6> pos{};
    for (int d = 0; d < 2; ++d) {
      pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(kCornerPair[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t start = rankPerm(pos, 8, 2, kCorner2Blocks) * 9u;
    p.cornerPair = buildSingleDb(t, kCorner2Entries, start, moves.size());
    report(p.cornerPair, "cornerPair");
  }
  // Final corners (3 corners).
  {
    auto t = buildCornerMoveTable(3, kCorner3Blocks, kCorner3Rank, 27u, moves, md);
    std::array<std::uint8_t, 6> pos{};
    for (int d = 0; d < 3; ++d) {
      pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(kFinalCorners[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t start = rankPerm(pos, 8, 3, kCorner3Blocks) * 27u;
    p.finalCorner3 = buildSingleDb(t, kCorner3Entries, start, moves.size());
    report(p.finalCorner3, "finalCorner3");
  }
  p.br = buildBrDb(md);
  report(p.br, "br");
  p.eo = buildEoDb(md);
  report(p.eo, "eo");
  {
    constexpr std::array<int, 4> kCorners = {4, 5, 6, 7};
    constexpr std::array<int, 4> kEdges = {8, 9, 10, 11};
    const char* names[4] = {"f2lPairFR", "f2lPairFL", "f2lPairBL", "f2lPairBR"};
    for (int k = 0; k < 4; ++k) {
      const std::size_t kk = static_cast<std::size_t>(k);
      p.f2lPair[kk] = buildPairDb(kCorners[kk], kEdges[kk], md);
      report(p.f2lPair[kk], names[k]);
    }
    p.urf = buildUrfDb(md);
    report(p.urf, "urf");
    p.dOrient = buildDOrientDb(md);
    report(p.dOrient, "dOrient");
    p.oriFull = buildOriFullDb(md);
    report(p.oriFull, "oriFull");
    p.ori5 = buildOri5Db(md);
    report(p.ori5, "ori5");
  }
  // Combined permutation PDBs for stage 3 (BR+U edges x DRB+URF) and
  // stage 4 (U edges x last 3 corners). Joint perm distances raise the
  // heuristic close to the true stage distance.
  {
    auto eTable = buildEdgePermTable(5, kEdge5PermBlocks, kEdge5PermRank, moves, md);
    auto cTable = buildCornerPermTable(2, kCorner2PermBlocks, kCorner2PermRank, moves, md);
    std::array<std::uint8_t, 6> ePos{};
    std::array<std::uint8_t, 6> cPos{};
    ePos[0] = 11; ePos[1] = 0; ePos[2] = 1; ePos[3] = 2; ePos[4] = 3;
    cPos[0] = 7; cPos[1] = 0;
    const std::uint32_t start =
        rankPerm(ePos, 12, 5, kEdge5PermBlocks) * kCorner2PermRank +
        rankPerm(cPos, 8, 2, kCorner2PermBlocks);
    p.pairPerm = buildCombinedPermDb(eTable, cTable, kEdge5PermRank,
                                     kCorner2PermRank, start, moves.size());
    report(p.pairPerm, "pairPerm");
  }
  {
    auto eTable = buildEdgePermTable(4, kEdge4PermBlocks, kEdge4PermRank, moves, md);
    auto cTable = buildCornerPermTable(3, kCorner3PermBlocks, kCorner3PermRank, moves, md);
    std::array<std::uint8_t, 6> ePos{};
    std::array<std::uint8_t, 6> cPos{};
    ePos[0] = 0; ePos[1] = 1; ePos[2] = 2; ePos[3] = 3;
    cPos[0] = 1; cPos[1] = 2; cPos[2] = 3;
    const std::uint32_t start =
        rankPerm(ePos, 12, 4, kEdge4PermBlocks) * kCorner3PermRank +
        rankPerm(cPos, 8, 3, kCorner3PermBlocks);
    p.finalPerm = buildCombinedPermDb(eTable, cTable, kEdge4PermRank,
                                      kCorner3PermRank, start, moves.size());
    report(p.finalPerm, "finalPerm");
  }
  // U edges (perm+orient) x DRB+URF (perm) for stage 3.
  {
    auto eTable = buildEdgeMoveTable(4, kEdge4Blocks, kEdge4Rank, 16u, moves, md);
    auto cTable = buildCornerPermTable(2, kCorner2PermBlocks, kCorner2PermRank, moves, md);
    std::array<std::uint8_t, 6> cPos{};
    cPos[0] = 7; cPos[1] = 0;
    const std::uint32_t start =
        rankPerm(cPos, 8, 2, kCorner2PermBlocks);
    p.uEdgeCorner = buildCombinedPermDb(eTable, cTable, kEdge4Entries,
                                        kCorner2PermRank, start, moves.size());
    report(p.uEdgeCorner, "uEdgeCorner");
  }
  return p;
}

std::mutex gPdbMutex;
std::optional<Pdbs> gPdbs;

const Pdbs& getPdbs() {
  const MoveData& md = getMoveData();
  std::lock_guard<std::mutex> lock(gPdbMutex);
  if (!gPdbs.has_value()) gPdbs = buildPdbs(md);
  return *gPdbs;
}

inline int pdbLookup(const std::vector<std::uint8_t>& db, std::uint32_t idx) {
  const std::uint8_t v = db[idx];
  return (v == 255u) ? 0 : static_cast<int>(v);
}

// ---------------------------------------------------------------------------
// Validity (face moves keep centres fixed, so inputs must have them fixed).
// ---------------------------------------------------------------------------
int permParity(const std::uint8_t* perm, int n) {
  int inv = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (perm[i] > perm[j]) ++inv;
    }
  }
  return inv % 2;
}

bool isValidCubieState(const CubeState& s) {
  for (int i = 0; i < 6; ++i) {
    if (s.centers[static_cast<std::size_t>(i)] != static_cast<std::uint8_t>(i)) {
      return false;
    }
  }
  bool seenC[8] = {};
  int twist = 0;
  for (int i = 0; i < 8; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (s.cp[ii] >= 8u || seenC[s.cp[ii]]) return false;
    seenC[s.cp[ii]] = true;
    if (s.co[ii] >= 3u) return false;
    twist += s.co[ii];
  }
  bool seenE[12] = {};
  int flip = 0;
  for (int i = 0; i < 12; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (s.ep[ii] >= 12u || seenE[s.ep[ii]]) return false;
    seenE[s.ep[ii]] = true;
    if (s.eo[ii] >= 2u) return false;
    flip += s.eo[ii];
  }
  if (twist % 3 != 0 || flip % 2 != 0) return false;
  if (permParity(s.cp.data(), 8) != permParity(s.ep.data(), 12)) {
    return false;
  }
  return true;
}

// Incremental sub-goal helpers (used inside the reported stages).
bool isCrossSolved(const CubeState& s) {
  for (int i = 4; i < 8; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (s.ep[ii] != static_cast<std::uint8_t>(i)) return false;
    if (s.eo[ii] != 0u) return false;
  }
  return true;
}
bool isF2lPairSolved(const CubeState& s, int corner, int edge) {
  const std::size_t cc = static_cast<std::size_t>(corner);
  const std::size_t ee = static_cast<std::size_t>(edge);
  return s.cp[cc] == static_cast<std::uint8_t>(corner) && s.co[cc] == 0u &&
         s.ep[ee] == static_cast<std::uint8_t>(edge) && s.eo[ee] == 0u;
}
bool isUrfSolved(const CubeState& s) {
  return s.cp[0] == 0u && s.co[0] == 0u;
}

// ---------------------------------------------------------------------------
// IDA* searcher. Heuristic = max of subproblem PDBs (admissible).
// ---------------------------------------------------------------------------
enum class StageKind {
  Squares, Mid1, Mid2, Mid3, Orient,
  BrPair, Urf, UEdges, Pairs, Final, Oll
};

struct Searcher {
  StageKind kind = StageKind::Squares;
  const std::vector<int>* allowed = nullptr;
  const Pdbs* pdbs = nullptr;
  const MoveData* mv = nullptr;
  int bestExceed = 0;
  std::vector<int> path;
  std::vector<int> solution;
  bool found = false;
  int abortKind = 0;  // 0 none, 1 timeout, 2 cancelled
  std::uint64_t nodes = 0;
  bool hasDeadline = false;
  std::chrono::steady_clock::time_point deadline{};
  std::stop_token externalToken{};

  int heuristic(const CubeState& s) const {
    // Cache cubie positions: cPos[cubie] = position, ePos[cubie] = position.
    std::array<std::uint8_t, 8> cPos{};
    std::array<std::uint8_t, 12> ePos{};
    for (int p = 0; p < 8; ++p) {
      cPos[static_cast<std::size_t>(s.cp[static_cast<std::size_t>(p)])] =
          static_cast<std::uint8_t>(p);
    }
    for (int p = 0; p < 12; ++p) {
      ePos[static_cast<std::size_t>(s.ep[static_cast<std::size_t>(p)])] =
          static_cast<std::uint8_t>(p);
    }
    auto cornerIdx = [&](const std::array<int, 4>& cubies, int k,
                         const std::array<std::uint32_t, 6>& blocks) {
      std::array<std::uint8_t, 6> pos{};
      std::array<std::uint8_t, 6> ori{};
      for (int d = 0; d < k; ++d) {
        const int cubie = cubies[static_cast<std::size_t>(d)];
        const std::uint8_t at = cPos[static_cast<std::size_t>(cubie)];
        pos[static_cast<std::size_t>(d)] = at;
        ori[static_cast<std::size_t>(d)] =
            s.co[static_cast<std::size_t>(at)];
      }
      const std::uint32_t oe = (k == 2) ? 9u : 27u;
      return rankPerm(pos, 8, k, blocks) * oe + cornerOrientIdx(ori, k);
    };
    auto edgeIdx = [&](const std::array<int, 4>& cubies, int k,
                       const std::array<std::uint32_t, 6>& blocks) {
      std::array<std::uint8_t, 6> pos{};
      std::array<std::uint8_t, 6> ori{};
      for (int d = 0; d < k; ++d) {
        const int cubie = cubies[static_cast<std::size_t>(d)];
        const std::uint8_t at = ePos[static_cast<std::size_t>(cubie)];
        pos[static_cast<std::size_t>(d)] = at;
        ori[static_cast<std::size_t>(d)] =
            s.eo[static_cast<std::size_t>(at)];
      }
      const std::uint32_t oe = (k == 3) ? 8u : 16u;
      return rankPerm(pos, 12, k, blocks) * oe + edgeOrientBits(ori, k);
    };
    auto pairId = [&](int cc, int ee) {
      const std::uint8_t atc = cPos[static_cast<std::size_t>(cc)];
      const std::uint8_t ate = ePos[static_cast<std::size_t>(ee)];
      const unsigned cOri = s.co[static_cast<std::size_t>(atc)];
      const unsigned eOri = s.eo[static_cast<std::size_t>(ate)];
      return static_cast<std::uint32_t>(
          ((static_cast<unsigned>(atc) * 3u + cOri) * 24u) +
          (static_cast<unsigned>(ate) * 2u + eOri));
    };
    auto edgePerm5 = [&]() {
      const std::array<int, 5> cubies = {11, 0, 1, 2, 3};
      std::array<std::uint8_t, 6> pos{};
      for (int d = 0; d < 5; ++d) {
        pos[static_cast<std::size_t>(d)] =
            ePos[static_cast<std::size_t>(cubies[static_cast<std::size_t>(d)])];
      }
      return rankPerm(pos, 12, 5, kEdge5PermBlocks);
    };
    auto cornerPerm2 = [&]() {
      const std::array<int, 2> cubies = {7, 0};
      std::array<std::uint8_t, 6> pos{};
      for (int d = 0; d < 2; ++d) {
        pos[static_cast<std::size_t>(d)] =
            cPos[static_cast<std::size_t>(cubies[static_cast<std::size_t>(d)])];
      }
      return rankPerm(pos, 8, 2, kCorner2PermBlocks);
    };
    auto uEdgePerm4 = [&]() {
      const std::array<int, 4> cubies = {0, 1, 2, 3};
      std::array<std::uint8_t, 6> pos{};
      for (int d = 0; d < 4; ++d) {
        pos[static_cast<std::size_t>(d)] =
            ePos[static_cast<std::size_t>(cubies[static_cast<std::size_t>(d)])];
      }
      return rankPerm(pos, 12, 4, kEdge4PermBlocks);
    };
    auto finalCornerPerm3 = [&]() {
      const std::array<int, 3> cubies = {1, 2, 3};
      std::array<std::uint8_t, 6> pos{};
      for (int d = 0; d < 3; ++d) {
        pos[static_cast<std::size_t>(d)] =
            cPos[static_cast<std::size_t>(cubies[static_cast<std::size_t>(d)])];
      }
      return rankPerm(pos, 8, 3, kCorner3PermBlocks);
    };
    const std::array<int, 4> de = {4, 5, 6, 7};
    const std::array<int, 4> dc3 = {4, 5, 6, 0};
    const std::array<int, 4> me = {8, 9, 10, 0};
    const std::array<int, 4> ue = {0, 1, 2, 3};
    const std::array<int, 4> cp = {7, 0, 0, 0};
    const std::array<int, 4> fc = {1, 2, 3, 0};
    int h = 0;
    h = (std::max)(h, pdbLookup(pdbs->dEdge, edgeIdx(de, 4, kEdge4Blocks)));
    if (kind == StageKind::Squares) {
      h = (std::max)(h, pdbLookup(pdbs->dOrient, cornerOriIndex(s.co)));
      return h;
    }
    if (kind == StageKind::Mid1) {
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[0], pairId(4, 8)));
      return h;
    }
    if (kind == StageKind::Mid2) {
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[0], pairId(4, 8)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[1], pairId(5, 9)));
      return h;
    }
    if (kind == StageKind::Mid3) {
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[0], pairId(4, 8)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[1], pairId(5, 9)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[2], pairId(6, 10)));
      return h;
    }
    h = (std::max)(h, pdbLookup(pdbs->dOrient, cornerOriIndex(s.co)));
    h = (std::max)(h, pdbLookup(pdbs->dCorner3, cornerIdx(dc3, 3, kCorner3Blocks)));
    if (kind == StageKind::Orient) {
      h = (std::max)(h, pdbLookup(pdbs->midEdge, edgeIdx(me, 3, kEdge3Blocks)));
      h = (std::max)(h, pdbLookup(pdbs->eo, eoIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[0], pairId(4, 8)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[1], pairId(5, 9)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[2], pairId(6, 10)));
      return h;
    }
    if (kind == StageKind::BrPair) {
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[0], pairId(4, 8)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[1], pairId(5, 9)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[2], pairId(6, 10)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[3], pairId(7, 11)));
      return h;
    }
    if (kind == StageKind::Oll) {
      h = (std::max)(h, pdbLookup(pdbs->midEdge, edgeIdx(me, 3, kEdge3Blocks)));
      h = (std::max)(h, pdbLookup(pdbs->eo, eoIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[0], pairId(4, 8)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[1], pairId(5, 9)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[2], pairId(6, 10)));
      h = (std::max)(h, pdbLookup(pdbs->f2lPair[3], pairId(7, 11)));
      h = (std::max)(h, pdbLookup(pdbs->oriFull, cornerOriIndex(s.co)));
      return h;
    }
    h = (std::max)(h, pdbLookup(pdbs->f2lPair[0], pairId(4, 8)));
    h = (std::max)(h, pdbLookup(pdbs->f2lPair[1], pairId(5, 9)));
    h = (std::max)(h, pdbLookup(pdbs->f2lPair[2], pairId(6, 10)));
    h = (std::max)(h, pdbLookup(pdbs->f2lPair[3], pairId(7, 11)));
    {
      const std::uint8_t at0 = cPos[0];
      const std::uint32_t uidx =
          static_cast<std::uint32_t>(at0 * 3 + s.co[static_cast<std::size_t>(at0)]);
      h = (std::max)(h, pdbLookup(pdbs->urf, uidx));
    }
    if (kind == StageKind::Urf) return h;
    h = (std::max)(h, pdbLookup(pdbs->midEdge, edgeIdx(me, 3, kEdge3Blocks)));
    h = (std::max)(h, pdbLookup(pdbs->eo, eoIdx(s)));
    h = (std::max)(h, pdbLookup(pdbs->uEdge, edgeIdx(ue, 4, kEdge4Blocks)));
    h = (std::max)(h, pdbLookup(pdbs->cornerPair, cornerIdx(cp, 2, kCorner2Blocks)));
    {
      const std::uint8_t at11 = ePos[11];
      const std::uint32_t bidx = static_cast<std::uint32_t>(
          at11 * 2 + s.eo[static_cast<std::size_t>(at11)]);
      h = (std::max)(h, pdbLookup(pdbs->br, bidx));
    }
    h = (std::max)(h, pdbLookup(pdbs->pairPerm,
                               edgePerm5() * kCorner2PermRank + cornerPerm2()));
    h = (std::max)(h, pdbLookup(pdbs->uEdgeCorner,
                               edgeIdx(ue, 4, kEdge4Blocks) * kCorner2PermRank +
                                   cornerPerm2()));
    h = (std::max)(h, pdbLookup(pdbs->ori5, cornerOriIndex(s.co)));
    if (kind == StageKind::UEdges || kind == StageKind::Pairs) return h;
    h = (std::max)(h, pdbLookup(pdbs->finalCorner3, cornerIdx(fc, 3, kCorner3Blocks)));
    h = (std::max)(h, pdbLookup(pdbs->oriFull, cornerOriIndex(s.co)));
    h = (std::max)(h, pdbLookup(pdbs->finalPerm,
                               uEdgePerm4() * kCorner3PermRank +
                                   finalCornerPerm3()));
    return h;
  }

  bool goal(const CubeState& s) const {
    if (kind == StageKind::Squares) return is_squares_solved(s);
    if (kind == StageKind::Mid1) {
      return isCrossSolved(s) && isF2lPairSolved(s, 4, 8);
    }
    if (kind == StageKind::Mid2) {
      return isCrossSolved(s) && isF2lPairSolved(s, 4, 8) &&
             isF2lPairSolved(s, 5, 9);
    }
    if (kind == StageKind::Mid3) {
      return isCrossSolved(s) && isF2lPairSolved(s, 4, 8) &&
             isF2lPairSolved(s, 5, 9) && isF2lPairSolved(s, 6, 10);
    }
    if (kind == StageKind::Orient) return is_f2l_minus_1_solved(s);
    if (kind == StageKind::Oll) {
      if (!isCrossSolved(s) || !isF2lPairSolved(s, 4, 8) ||
          !isF2lPairSolved(s, 5, 9) || !isF2lPairSolved(s, 6, 10) ||
          !isF2lPairSolved(s, 7, 11)) {
        return false;
      }
      for (int i = 0; i < 8; ++i) {
        if (s.co[static_cast<std::size_t>(i)] != 0u) return false;
      }
      for (int i = 0; i < 12; ++i) {
        if (s.eo[static_cast<std::size_t>(i)] != 0u) return false;
      }
      return true;
    }
    if (kind == StageKind::BrPair) {
      return isCrossSolved(s) && isF2lPairSolved(s, 4, 8) &&
             isF2lPairSolved(s, 5, 9) && isF2lPairSolved(s, 6, 10) &&
             isF2lPairSolved(s, 7, 11);
    }
    if (kind == StageKind::Urf) {
      return isCrossSolved(s) && isF2lPairSolved(s, 4, 8) &&
             isF2lPairSolved(s, 5, 9) && isF2lPairSolved(s, 6, 10) &&
             isF2lPairSolved(s, 7, 11) && isUrfSolved(s);
    }
    if (kind == StageKind::UEdges || kind == StageKind::Pairs) {
      return is_corner_edge_pairs_solved(s);
    }
    return is_final_corners_solved(s);
  }

  bool dfs(const CubeState& cur, int g, int prevGroup, int bound) {
    if (abortKind != 0) return true;
    if (goal(cur)) {
      solution = path;
      found = true;
      return true;
    }
    if (((++nodes) & 1023u) == 0u) {
      if (externalToken.stop_requested()) {
        abortKind = 2;
        return true;
      }
      if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
        abortKind = 1;
        return true;
      }
    }
    const int h = heuristic(cur);
    const int f = g + h;
    if (f > bound) {
      if (f < bestExceed) bestExceed = f;
      return false;
    }
    struct Cand {
      int h = 0;
      int f = 0;
      int mvIdx = -1;
      CubeState next;
    };
    std::array<Cand, 18> cands;
    int n = 0;
    for (int mvIdx : *allowed) {
      const int group = mv->faceGroup[static_cast<std::size_t>(mvIdx)];
      if (group == prevGroup) continue;
      // Opposite faces commute; keep only one order for efficiency.
      if (prevGroup != -1 &&
          ((prevGroup == 0 && group == 3) || (prevGroup == 3 && group == 0) ||
           (prevGroup == 1 && group == 4) || (prevGroup == 4 && group == 1) ||
           (prevGroup == 2 && group == 5) || (prevGroup == 5 && group == 2)) &&
          group > prevGroup) {
        continue;
      }
      const CubeState next = fastApply(cur, mvIdx, *mv);
      const int hh = heuristic(next);
      const int ff = g + 1 + hh;
      if (ff > bound) {
        if (ff < bestExceed) bestExceed = ff;
        continue;
      }
      Cand& c = cands[static_cast<std::size_t>(n)];
      c.h = hh;
      c.f = ff;
      c.mvIdx = mvIdx;
      c.next = next;
      ++n;
    }
    std::sort(cands.begin(), cands.begin() + n,
              [](const Cand& a, const Cand& b) {
                if (a.f != b.f) return a.f < b.f;
                return a.h < b.h;
              });
    for (int i = 0; i < n; ++i) {
      const int mvIdx = cands[static_cast<std::size_t>(i)].mvIdx;
      path.push_back(mvIdx);
      if (dfs(cands[static_cast<std::size_t>(i)].next, g + 1,
              mv->faceGroup[static_cast<std::size_t>(mvIdx)], bound)) {
        return true;
      }
      path.pop_back();
      if (abortKind != 0) return true;
    }
    return false;
  }
};

std::expected<std::vector<int>, SolveError> runIda(
    const CubeState& start, StageKind kind, const std::vector<int>& allowed,
    int stageLimit, bool hasDeadline,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token externalToken) {
  const MoveData& md = getMoveData();
  const Pdbs& pdbs = getPdbs();
  Searcher searcher;
  searcher.kind = kind;
  searcher.allowed = &allowed;
  searcher.pdbs = &pdbs;
  searcher.mv = &md;
  searcher.hasDeadline = hasDeadline;
  searcher.deadline = deadline;
  searcher.externalToken = externalToken;
  if (searcher.goal(start)) return std::vector<int>{};
  int bound = searcher.heuristic(start);
  while (true) {
    if (bound > stageLimit) return std::unexpected(SolveError::Timeout);
    if (externalToken.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    searcher.bestExceed = 2147483647;
    searcher.path.clear();
    searcher.found = false;
    searcher.abortKind = 0;
    (void)searcher.dfs(start, 0, -1, bound);
    if (searcher.found) return searcher.solution;
    if (searcher.abortKind == 2) return std::unexpected(SolveError::Cancelled);
    if (searcher.abortKind == 1) return std::unexpected(SolveError::Timeout);
    if (externalToken.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    if (searcher.bestExceed == 2147483647) {
      return std::unexpected(SolveError::Unsolvable);
    }
    bound = searcher.bestExceed;
  }
}

std::vector<std::string> tokensFor(const std::vector<int>& idxs, const MoveData& md) {
  std::vector<std::string> out;
  out.reserve(idxs.size());
  for (int i : idxs) out.push_back(md.tokens[static_cast<std::size_t>(i)]);
  return out;
}

bool movesAllowed(const std::vector<std::string>& tokens,
                  const std::vector<std::string>& allowed) {
  for (const std::string& t : tokens) {
    bool ok = false;
    for (const std::string& a : allowed) {
      if (t == a) {
        ok = true;
        break;
      }
    }
    if (!ok) return false;
  }
  return true;
}

// --- Commutator generators for the U-layer (PLL) BFS. Both algorithms are
// simulated below and in tests to preserve F2L and all orientations while
// 3-cycling U edges / U corners respectively (edgeAlg preserves corners,
// cornerAlg preserves edges). Together with U rotations they generate the
// F2L-preserving U-permutation group.
std::vector<std::vector<int>> pllGenerators(const MoveData& md,
                                            bool includeEdgeAlg) {
  auto parseToIdx = [&](const std::string& text) {
    auto parsed = parse_moves(text);
    std::vector<int> v;
    if (!parsed) return v;
    for (const Move& m : *parsed) {
      int found = -1;
      for (int i = 0; i < 18; ++i) {
        if (md.tokens[static_cast<std::size_t>(i)] == m.token) {
          found = i;
          break;
        }
      }
      if (found >= 0) v.push_back(found);
    }
    return v;
  };
  const std::vector<int> edgeAlg =
      parseToIdx("R' U R' U' R' U' R' U R U R2");
  const std::vector<int> cornerAlg =
      parseToIdx("R2 F2 R' B' R F2 R' B R'");
  auto invOf = [&](const std::vector<int>& seq) {
    std::vector<int> inv;
    for (auto it = seq.rbegin(); it != seq.rend(); ++it) {
      const int idx = *it;
      const std::string tok = md.tokens[static_cast<std::size_t>(idx)];
      std::string invTok;
      if (tok.size() == 2 && tok[1] == '\'') {
        invTok = std::string(1, tok[0]);
      } else if (tok.size() == 2 && tok[1] == '2') {
        invTok = tok;
      } else {
        invTok = tok + "'";
      }
      for (int i = 0; i < 18; ++i) {
        if (md.tokens[static_cast<std::size_t>(i)] == invTok) {
          inv.push_back(i);
          break;
        }
      }
    }
    return inv;
  };
  const std::vector<int> uIdx = parseToIdx("U");
  const std::vector<int> u2Idx = parseToIdx("U2");
  const std::vector<int> upIdx = parseToIdx("U'");
  std::vector<std::vector<int>> gens;
  gens.push_back(uIdx);
  gens.push_back(u2Idx);
  gens.push_back(upIdx);
  if (includeEdgeAlg) {
    gens.push_back(edgeAlg);
    gens.push_back(invOf(edgeAlg));
  }
  gens.push_back(cornerAlg);
  gens.push_back(invOf(cornerAlg));
  return gens;
}

std::uint32_t uPermKey(const CubeState& s) {
  std::array<std::uint8_t, 6> ePos{};
  std::array<std::uint8_t, 6> cPos{};
  for (int d = 0; d < 4; ++d) {
    const std::size_t dd = static_cast<std::size_t>(d);
    int at = -1;
    for (int p = 0; p < 12; ++p) {
      if (s.ep[static_cast<std::size_t>(p)] == static_cast<std::uint8_t>(d)) {
        at = p;
        break;
      }
    }
    ePos[dd] = static_cast<std::uint8_t>(at);
    int cat = -1;
    for (int p = 0; p < 8; ++p) {
      if (s.cp[static_cast<std::size_t>(p)] == static_cast<std::uint8_t>(d)) {
        cat = p;
        break;
      }
    }
    cPos[dd] = static_cast<std::uint8_t>(cat);
  }
  return rankPerm(ePos, 12, 4, kEdge4PermBlocks) * 1680u +
         rankPerm(cPos, 8, 4, kCorner4PermBlocks);
}

bool pllGoalPairs(const CubeState& s) {
  // Prefer a non-solved P3 state so the final stage still has 3 corners to
  // solve (fall back to any P3 state via the caller if this is unreachable).
  return is_corner_edge_pairs_solved(s) && !is_solved(s);
}
bool pllGoalSolved(const CubeState& s) { return is_solved(s); }

// BFS over the F2L-preserving U-permutation group. Returns move indices or
// nullopt when unreachable (should not happen from a valid Oll state).
std::optional<std::vector<int>> solvePllBfs(
    const CubeState& start, const MoveData& md, bool includeEdgeAlg,
    bool goalIsPairs) {
  auto goalFn = goalIsPairs ? &pllGoalPairs : &pllGoalSolved;
  if (goalFn(start)) return std::vector<int>{};
  const std::vector<std::vector<int>> gens = pllGenerators(md, includeEdgeAlg);
  const std::uint32_t startKey = uPermKey(start);
  std::vector<std::uint32_t> queue;
  std::map<std::uint32_t, CubeState> states;
  std::map<std::uint32_t, std::uint32_t> parent;
  std::map<std::uint32_t, int> parentGen;
  queue.reserve(600);
  queue.push_back(startKey);
  states[startKey] = start;
  parent[startKey] = startKey;
  std::size_t head = 0;
  std::uint32_t foundKey = startKey;
  bool found = false;
  while (head < queue.size() && !found) {
    const std::uint32_t key = queue[head++];
    const CubeState cur = states[key];
    for (std::size_t gi = 0; gi < gens.size(); ++gi) {
      CubeState next = cur;
      for (int mv : gens[gi]) next = fastApply(next, mv, md);
      const std::uint32_t nkey = uPermKey(next);
      if (states.find(nkey) != states.end()) continue;
      states[nkey] = next;
      parent[nkey] = key;
      parentGen[nkey] = static_cast<int>(gi);
      if (goalFn(next)) {
        foundKey = nkey;
        found = true;
        break;
      }
      queue.push_back(nkey);
    }
  }
  if (!found) return std::nullopt;
  std::vector<int> genPath;
  for (std::uint32_t k = foundKey; k != startKey; k = parent[k]) {
    genPath.push_back(parentGen[k]);
  }
  std::vector<int> out;
  for (auto it = genPath.rbegin(); it != genPath.rend(); ++it) {
    const std::vector<int>& g = gens[static_cast<std::size_t>(*it)];
    out.insert(out.end(), g.begin(), g.end());
  }
  return out;
}

}  // namespace

std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options) {
  if (options.stop_token.stop_requested()) {
    return std::unexpected(SolveError::Cancelled);
  }
  if (!isValidCubieState(state)) {
    return std::unexpected(SolveError::Unsolvable);
  }
  const MoveData& md = getMoveData();
  const std::vector<int> faceIdx = faceMoveIndices();

  const auto t0 = std::chrono::steady_clock::now();
  const bool hasDeadline = options.timeout_ms > 0;
  const auto deadline =
      hasDeadline ? t0 + std::chrono::milliseconds(options.timeout_ms) : t0;
  int stageLimit = options.max_depth;
  if (stageLimit < 0) stageLimit = 0;

  CubeState cur = state;
  Solution sol;
  auto stageT0 = std::chrono::steady_clock::now();
  auto logStage = [&](const std::string& nm, const std::vector<std::string>& tk) {
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - stageT0).count();
    std::cout << std::format("heise stage {}: {} moves, {:.1f} ms\n", nm, tk.size(),
                             ms);
    stageT0 = now;
  };

  // Stage 1: squares.
  {
    auto r = runIda(cur, StageKind::Squares, faceIdx, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("squares")) ||
        !is_squares_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"squares", toks});
    logStage("squares", toks);
  }
  // Stage 2: F2L-1 (pair squares + orient last-layer edges). Solved as
  // three F2L sub-pairs plus orientation so each IDA* stays deep only
  // within one pair; concatenated moves form the reported stage.
  {
    std::vector<std::string> toks;
    const std::array<StageKind, 4> subs = {StageKind::Mid1, StageKind::Mid2,
                                           StageKind::Mid3, StageKind::Orient};
    for (StageKind kind : subs) {
      auto subT0 = std::chrono::steady_clock::now();
      auto r = runIda(cur, kind, faceIdx, stageLimit, hasDeadline,
                      deadline, options.stop_token);
      if (!r) return std::unexpected(r.error());
      const std::vector<int> idxs = *r;
      const std::vector<std::string> subToks = tokensFor(idxs, md);
      for (int mv : idxs) cur = fastApply(cur, mv, md);
      auto subNow = std::chrono::steady_clock::now();
      double subMs =
          std::chrono::duration<double, std::milli>(subNow - subT0).count();
      std::cout << std::format("heise   substep {}: {} moves, {:.1f} ms\n",
                               static_cast<int>(kind), subToks.size(), subMs);
      toks.insert(toks.end(), subToks.begin(), subToks.end());
    }
    if (!movesAllowed(toks, allowed_moves_for_stage("f2l_minus_1")) ||
        !is_f2l_minus_1_solved(cur)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    sol.stages.push_back(StageSolution{"f2l_minus_1", toks});
    logStage("f2l_minus_1", toks);
  }
  // Stage 3: corner-edge pairs (last 5 edges + 2 corners). Complete F2L,
  // orient the last layer, then BFS over the F2L-preserving U-permutation
  // commutator group (U rotations + edge/corner 3-cycles) to solve the last
  // 5 edges + 2 corners while leaving 3 corners. This is the Heise
  // corner-edge pair step, executed exactly via the commutator group.
  {
    std::vector<std::string> toks;
    const std::array<StageKind, 2> subs = {StageKind::BrPair, StageKind::Oll};
    for (StageKind kind : subs) {
      auto subT0 = std::chrono::steady_clock::now();
      auto r = runIda(cur, kind, faceIdx, stageLimit, hasDeadline,
                      deadline, options.stop_token);
      if (!r) return std::unexpected(r.error());
      const std::vector<int> idxs = *r;
      const std::vector<std::string> subToks = tokensFor(idxs, md);
      for (int mv : idxs) cur = fastApply(cur, mv, md);
      auto subNow = std::chrono::steady_clock::now();
      double subMs =
          std::chrono::duration<double, std::milli>(subNow - subT0).count();
      std::cout << std::format("heise   substep {}: {} moves, {:.1f} ms\n",
                               static_cast<int>(kind), subToks.size(), subMs);
      toks.insert(toks.end(), subToks.begin(), subToks.end());
    }
    if (options.stop_token.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    auto subT0 = std::chrono::steady_clock::now();
    std::optional<std::vector<int>> bfs;
    if (is_corner_edge_pairs_solved(cur)) {
      bfs = std::vector<int>{};
    } else {
      bfs = solvePllBfs(cur, md, true, true);
      if (!bfs.has_value()) {
        // Fall back to allowing the solved state as a P3 target.
        // (solvePllBfs uses the unsolved-target goal; retry via Pairs IDA*
        // below in the !has_value branch.)
      }
    }
    if (!bfs.has_value()) {
      auto r = runIda(cur, StageKind::Pairs, faceIdx, stageLimit, hasDeadline,
                      deadline, options.stop_token);
      if (!r) return std::unexpected(r.error());
      const std::vector<int> idxs = *r;
      const std::vector<std::string> subToks = tokensFor(idxs, md);
      for (int mv : idxs) cur = fastApply(cur, mv, md);
      toks.insert(toks.end(), subToks.begin(), subToks.end());
    } else {
      const std::vector<int> idxs = *bfs;
      const std::vector<std::string> subToks = tokensFor(idxs, md);
      for (int mv : idxs) cur = fastApply(cur, mv, md);
      toks.insert(toks.end(), subToks.begin(), subToks.end());
    }
    {
      auto subNow = std::chrono::steady_clock::now();
      double subMs =
          std::chrono::duration<double, std::milli>(subNow - subT0).count();
      std::cout << std::format("heise   substep pll: {} moves, {:.1f} ms\n",
                               toks.size(), subMs);
    }
    if (!movesAllowed(toks, allowed_moves_for_stage("corner_edge_pairs")) ||
        !is_corner_edge_pairs_solved(cur)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    sol.stages.push_back(StageSolution{"corner_edge_pairs", toks});
    logStage("corner_edge_pairs", toks);
  }
  // Stage 4: final corners (commutator). BFS over the corner 3-cycle
  // commutator group (U rotations + corner 3-cycle) to solve the last 3
  // corners while restoring the last layer.
  {
    if (options.stop_token.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    auto r = solvePllBfs(cur, md, false, false);
    std::vector<int> idxs;
    if (r.has_value()) {
      idxs = *r;
    } else {
      auto rr = runIda(cur, StageKind::Final, faceIdx, stageLimit,
                       hasDeadline, deadline, options.stop_token);
      if (!rr) return std::unexpected(rr.error());
      idxs = *rr;
    }
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("final_corners")) ||
        !is_final_corners_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"final_corners", toks});
    logStage("final_corners", toks);
  }
  if (!is_solved(cur)) return std::unexpected(SolveError::Unsolvable);
  return sol;
}

}  // namespace rubik::heise
