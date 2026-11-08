#include "solvers/roux/solver.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include "core/moves.hpp"
#include "core/pdb.hpp"
#include "core/searcher.hpp"

namespace rubik::roux {

using core::IdaSearcher;
using core::bfsDb;
using core::pdbLookup;

// ---------------------------------------------------------------------------
// Corners 0..7, edges 0..11 as in core/cube.hpp.
// FB: corners 5,6 (DLF,DBL), edges 6,9,10 (DL,FL,BL) + L centre.
// SB: corners 4,7 (DFR,DRB), edges 4,8,11 (DR,FR,BR) + R centre.
// CMLL: corners 0..3 (U corners). LSE edges: 0,1,2,3,5,7 (UR,UF,UL,UB,DF,DB).
// ---------------------------------------------------------------------------

// U-corner rotation table: kUCornerRot[k][p] = cubie at U position p in
// U^k(solved). Derived from the frozen core (verified in tests).
constexpr std::array<std::array<std::uint8_t, 4>, 4> kUCornerRot = {{
    {0, 1, 2, 3},
    {3, 0, 1, 2},
    {2, 3, 0, 1},
    {1, 2, 3, 0},
}};

// M-centre rotation table: kMCentreRot[k][f] = centre cubie at face f in
// M^k(solved), faces in URFDLB order. Derived from the frozen core.
constexpr std::array<std::array<std::uint8_t, 6>, 4> kMCentreRot = {{
    {0, 1, 2, 3, 4, 5},
    {5, 1, 0, 2, 4, 3},
    {3, 1, 5, 0, 4, 2},
    {2, 1, 3, 5, 4, 0},
}};

bool is_first_block_solved(const CubeState& state) {
  if (state.centers[4] != 4u) return false;  // L centre
  if (state.cp[5] != 5u || state.co[5] != 0u) return false;
  if (state.cp[6] != 6u || state.co[6] != 0u) return false;
  if (state.ep[6] != 6u || state.eo[6] != 0u) return false;
  if (state.ep[9] != 9u || state.eo[9] != 0u) return false;
  if (state.ep[10] != 10u || state.eo[10] != 0u) return false;
  return true;
}

bool is_second_block_solved(const CubeState& state) {
  if (!is_first_block_solved(state)) return false;
  if (state.centers[1] != 1u) return false;  // R centre
  if (state.cp[4] != 4u || state.co[4] != 0u) return false;
  if (state.cp[7] != 7u || state.co[7] != 0u) return false;
  if (state.ep[4] != 4u || state.eo[4] != 0u) return false;
  if (state.ep[8] != 8u || state.eo[8] != 0u) return false;
  if (state.ep[11] != 11u || state.eo[11] != 0u) return false;
  return true;
}

bool is_cmll_solved(const CubeState& state) {
  if (!is_second_block_solved(state)) return false;
  for (int i = 0; i < 4; ++i) {
    if (state.co[static_cast<std::size_t>(i)] != 0u) return false;
  }
  for (int k = 0; k < 4; ++k) {
    bool match = true;
    for (int p = 0; p < 4; ++p) {
      if (state.cp[static_cast<std::size_t>(p)] !=
          kUCornerRot[static_cast<std::size_t>(k)][static_cast<std::size_t>(p)]) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

bool is_lse_solved(const CubeState& state) { return is_solved(state); }

std::vector<std::string> stage_names() {
  return {"first_block", "second_block", "cmll", "lse"};
}

std::vector<std::string> first_block_allowed_moves() {
  std::vector<std::string> out;
  for (const Move& m : all_face_moves()) out.push_back(m.token);
  return out;
}

std::vector<std::string> second_block_allowed_moves() {
  return first_block_allowed_moves();
}

std::vector<std::string> cmll_allowed_moves() {
  return first_block_allowed_moves();
}

std::vector<std::string> lse_allowed_moves() {
  return {"M", "M'", "M2", "U", "U'", "U2"};
}

std::vector<std::string> allowed_moves_for_stage(const std::string& stage_name) {
  if (stage_name == "first_block") return first_block_allowed_moves();
  if (stage_name == "second_block") return second_block_allowed_moves();
  if (stage_name == "cmll") return cmll_allowed_moves();
  if (stage_name == "lse") return lse_allowed_moves();
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
    ori[static_cast<std::size_t>(d)] =
        static_cast<std::uint8_t>(idx % 3u);
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
constexpr std::array<std::uint32_t, 6> kEdge3Blocks = {110, 10, 1, 0, 0, 0};
constexpr std::array<std::uint32_t, 6> kCorner4Blocks = {210, 30, 5, 1, 0, 0};
constexpr std::array<std::uint32_t, 6> kLseBlocks = {120, 24, 6, 2, 1, 1};
constexpr std::uint32_t kCorner2Rank = 56u;
constexpr std::uint32_t kEdge3Rank = 1320u;
constexpr std::uint32_t kCorner4Rank = 1680u;
constexpr std::uint32_t kCorner2Entries = 504u;    // 56*9
constexpr std::uint32_t kEdge3Entries = 10560u;    // 1320*8
constexpr std::uint32_t kBlockEntries = 5322240u;  // 504*10560
constexpr std::uint32_t kCmllEntries = 136080u;    // 1680*81
constexpr std::uint32_t kLseEdgeEntries = 46080u;  // 720*64
constexpr std::uint32_t kLseEntries = 737280u;     // 46080*16

// Cubie lists in pattern order.
constexpr std::array<int, 2> kFbCorners = {5, 6};
constexpr std::array<int, 3> kFbEdges = {6, 9, 10};
constexpr std::array<int, 2> kSbCorners = {4, 7};
constexpr std::array<int, 3> kSbEdges = {4, 8, 11};
constexpr std::array<int, 4> kCmllCorners = {0, 1, 2, 3};
constexpr std::array<int, 6> kLseEdges = {0, 1, 2, 3, 5, 7};
// LSE edge positions (slots): actual position -> slot, slot -> actual.
constexpr std::array<int, 12> kPosToSlot = {0, 1, 2, 3, -1, 4, -1, 5, -1, -1, -1, -1};
constexpr std::array<int, 6> kSlotToPos = {0, 1, 2, 3, 5, 7};

// ---------------------------------------------------------------------------
// Move data derived from the frozen core (verified against rubik::apply in
// tests). 21 tokens: 18 face moves + M, M', M2. Indices 0..17 follow
// all_face_moves() order; 18=M, 19=M', 20=M2.
// ---------------------------------------------------------------------------
struct MoveData {
  std::array<std::array<std::uint8_t, 8>, 21> cmCp{};
  std::array<std::array<std::uint8_t, 8>, 21> cmCo{};
  std::array<std::array<std::uint8_t, 12>, 21> cmEp{};
  std::array<std::array<std::uint8_t, 12>, 21> cmEo{};
  std::array<std::array<std::uint8_t, 6>, 21> cmCent{};
  std::array<std::array<std::uint8_t, 8>, 21> destC{};
  std::array<std::array<std::uint8_t, 8>, 21> addC{};
  std::array<std::array<std::uint8_t, 12>, 21> destE{};
  std::array<std::array<std::uint8_t, 12>, 21> addE{};
  std::array<std::string, 21> tokens{};
  std::array<int, 21> faceGroup{};
};

MoveData buildMoveData() {
  MoveData md;
  std::vector<std::string> toks;
  for (const Move& m : all_face_moves()) toks.push_back(m.token);
  toks.push_back("M");
  toks.push_back("M'");
  toks.push_back("M2");
  const CubeState solvedState = CubeState::solved();
  for (std::size_t i = 0; i < 21u; ++i) {
    const CubeState after = apply(solvedState, Move{toks[i]});
    md.tokens[i] = toks[i];
    md.faceGroup[i] = (i < 18u) ? static_cast<int>(i / 3u) : 6;
    for (std::size_t p = 0; p < 8u; ++p) {
      md.cmCp[i][p] = after.cp[p];
      md.cmCo[i][p] = after.co[p];
    }
    for (std::size_t p = 0; p < 12u; ++p) {
      md.cmEp[i][p] = after.ep[p];
      md.cmEo[i][p] = after.eo[p];
    }
    for (std::size_t p = 0; p < 6u; ++p) md.cmCent[i][p] = after.centers[p];
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

inline CubeState fastApply(const CubeState& s, int moveIdx, const MoveData& md) {
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
  for (std::size_t p = 0; p < 6u; ++p) {
    n.centers[p] = s.centers[md.cmCent[mi][p]];
  }
  return n;
}

// ---------------------------------------------------------------------------
// Pattern indices from a cubie state.
// ---------------------------------------------------------------------------
int findCornerPos(const CubeState& s, int cubie) {
  for (int p = 0; p < 8; ++p) {
    if (s.cp[static_cast<std::size_t>(p)] == static_cast<std::uint8_t>(cubie)) return p;
  }
  return -1;
}

int findEdgePos(const CubeState& s, int cubie) {
  for (int p = 0; p < 12; ++p) {
    if (s.ep[static_cast<std::size_t>(p)] == static_cast<std::uint8_t>(cubie)) return p;
  }
  return -1;
}

std::uint32_t fbCornerIdx(const CubeState& s) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < 2; ++d) {
    const int cubie = kFbCorners[static_cast<std::size_t>(d)];
    const int at = findCornerPos(s, cubie);
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] = s.co[static_cast<std::size_t>(at)];
  }
  return rankPerm(pos, 8, 2, kCorner2Blocks) * 9u + cornerOrientIdx(ori, 2);
}

std::uint32_t fbEdgeIdx(const CubeState& s) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < 3; ++d) {
    const int cubie = kFbEdges[static_cast<std::size_t>(d)];
    const int at = findEdgePos(s, cubie);
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] = s.eo[static_cast<std::size_t>(at)];
  }
  return rankPerm(pos, 12, 3, kEdge3Blocks) * 8u + edgeOrientBits(ori, 3);
}

std::uint32_t sbCornerIdx(const CubeState& s) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < 2; ++d) {
    const int cubie = kSbCorners[static_cast<std::size_t>(d)];
    const int at = findCornerPos(s, cubie);
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] = s.co[static_cast<std::size_t>(at)];
  }
  return rankPerm(pos, 8, 2, kCorner2Blocks) * 9u + cornerOrientIdx(ori, 2);
}

std::uint32_t sbEdgeIdx(const CubeState& s) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < 3; ++d) {
    const int cubie = kSbEdges[static_cast<std::size_t>(d)];
    const int at = findEdgePos(s, cubie);
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] = s.eo[static_cast<std::size_t>(at)];
  }
  return rankPerm(pos, 12, 3, kEdge3Blocks) * 8u + edgeOrientBits(ori, 3);
}

std::uint32_t cmllIdx(const CubeState& s) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < 4; ++d) {
    const int cubie = kCmllCorners[static_cast<std::size_t>(d)];
    const int at = findCornerPos(s, cubie);
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] = s.co[static_cast<std::size_t>(at)];
  }
  return rankPerm(pos, 8, 4, kCorner4Blocks) * 81u + cornerOrientIdx(ori, 4);
}

std::uint32_t lseEdgeIdx(const CubeState& s) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < 6; ++d) {
    const int cubie = kLseEdges[static_cast<std::size_t>(d)];
    const int at = findEdgePos(s, cubie);
    const int slot = (at >= 0) ? kPosToSlot[static_cast<std::size_t>(at)] : -1;
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(slot);
    ori[static_cast<std::size_t>(d)] = s.eo[static_cast<std::size_t>(at)];
  }
  return rankPerm(pos, 6, 6, kLseBlocks) * 64u + edgeOrientBits(ori, 6);
}

int centreRotOf(const CubeState& s) {
  for (int k = 0; k < 4; ++k) {
    bool match = true;
    for (int f = 0; f < 6; ++f) {
      if (s.centers[static_cast<std::size_t>(f)] !=
          kMCentreRot[static_cast<std::size_t>(k)][static_cast<std::size_t>(f)]) {
        match = false;
        break;
      }
    }
    if (match) return k;
  }
  return -1;
}

int cornerRotOf(const CubeState& s) {
  for (int i = 0; i < 4; ++i) {
    if (s.co[static_cast<std::size_t>(i)] != 0u) return -1;
  }
  for (int k = 0; k < 4; ++k) {
    bool match = true;
    for (int p = 0; p < 4; ++p) {
      if (s.cp[static_cast<std::size_t>(p)] !=
          kUCornerRot[static_cast<std::size_t>(k)][static_cast<std::size_t>(p)]) {
        match = false;
        break;
      }
    }
    if (match) return k;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Pattern move tables and PDB BFS.
// ---------------------------------------------------------------------------
std::vector<int> faceMoveIndices() {
  std::vector<int> out;
  for (int i = 0; i < 18; ++i) out.push_back(i);
  return out;
}

std::vector<int> lseMoveIndices() {
  // M, M', M2, U, U', U2 in MoveData index space.
  return {18, 19, 20, 0, 1, 2};
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

// LSE edge move table over slot positions (n=6) for the 6 LSE moves.
std::vector<std::uint32_t> buildLseEdgeMoveTable(const std::vector<int>& moves,
                                                 const MoveData& md) {
  std::vector<std::uint32_t> table(static_cast<std::size_t>(kLseEdgeEntries) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < kLseEdgeEntries; ++idx) {
    std::array<std::uint8_t, 6> pos{};
    std::array<std::uint8_t, 6> ori{};
    unrankPerm(idx / 64u, 6, 6, kLseBlocks, pos);
    edgeOrientUnrank(idx % 64u, 6, ori);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const int mv = moves[mi];
      const std::size_t mmi = static_cast<std::size_t>(mv);
      std::array<std::uint8_t, 6> npos{};
      std::array<std::uint8_t, 6> nori{};
      for (int d = 0; d < 6; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        const int actual = kSlotToPos[pos[dd]];
        const std::uint8_t np = md.destE[mmi][static_cast<std::size_t>(actual)];
        const int slot = kPosToSlot[np];
        npos[dd] = static_cast<std::uint8_t>(slot);
        nori[dd] = static_cast<std::uint8_t>(ori[dd] ^ md.addE[mmi][np]);
      }
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          rankPerm(npos, 6, 6, kLseBlocks) * 64u + edgeOrientBits(nori, 6);
    }
  }
  return table;
}

std::vector<std::uint8_t> buildCombinedDb(
    const std::vector<std::uint32_t>& cornerTable,
    const std::vector<std::uint32_t>& edgeTable, std::uint32_t startIdx) {
  // Single-source BFS over the combined (corner x edge) pattern space.
  return bfsDb(kBlockEntries, startIdx, [&](std::uint32_t idx) {
    std::vector<std::uint32_t> out;
    out.reserve(18);
    const std::uint32_t cIdx = idx / kEdge3Entries;
    const std::uint32_t eIdx = idx % kEdge3Entries;
    for (int mi = 0; mi < 18; ++mi) {
      const std::uint32_t nc =
          cornerTable[static_cast<std::size_t>(cIdx) * 18u + static_cast<std::size_t>(mi)];
      const std::uint32_t ne =
          edgeTable[static_cast<std::size_t>(eIdx) * 18u + static_cast<std::size_t>(mi)];
      out.push_back(nc * kEdge3Entries + ne);
    }
    return out;
  });
}

struct Pdbs {
  std::vector<std::uint8_t> fb;
  std::vector<std::uint8_t> sb;
  std::vector<std::uint8_t> cmll;
  std::vector<std::uint8_t> lse;
};

Pdbs buildPdbs(const MoveData& md) {
  Pdbs p;
  const std::vector<int> faceMoves = faceMoveIndices();
  auto report = [](const std::vector<std::uint8_t>& db, const char* name) {
    std::size_t vis = 0;
    int mx = 0;
    for (std::uint8_t v : db) {
      if (v != 255u) ++vis;
      if (v != 255u && static_cast<int>(v) > mx) mx = static_cast<int>(v);
    }
    std::cout << std::format("roux pdb {}: {}/{} visited max {}\n", name, vis,
                             db.size(), mx);
  };

  // FB combined pattern (2 corners + 3 edges).
  {
    auto cTable = buildCornerMoveTable(2, kCorner2Blocks, kCorner2Rank, 9u, faceMoves, md);
    auto eTable = buildEdgeMoveTable(3, kEdge3Blocks, kEdge3Rank, 8u, faceMoves, md);
    std::array<std::uint8_t, 6> cPos{};
    std::array<std::uint8_t, 6> ePos{};
    cPos[0] = 5; cPos[1] = 6;
    ePos[0] = 6; ePos[1] = 9; ePos[2] = 10;
    const std::uint32_t start =
        (rankPerm(cPos, 8, 2, kCorner2Blocks) * 9u) * kEdge3Entries +
        (rankPerm(ePos, 12, 3, kEdge3Blocks) * 8u);
    p.fb = buildCombinedDb(cTable, eTable, start);
    report(p.fb, "fb");
  }
  // SB combined pattern (2 corners + 3 edges).
  {
    auto cTable = buildCornerMoveTable(2, kCorner2Blocks, kCorner2Rank, 9u, faceMoves, md);
    auto eTable = buildEdgeMoveTable(3, kEdge3Blocks, kEdge3Rank, 8u, faceMoves, md);
    std::array<std::uint8_t, 6> cPos{};
    std::array<std::uint8_t, 6> ePos{};
    cPos[0] = 4; cPos[1] = 7;
    ePos[0] = 4; ePos[1] = 8; ePos[2] = 11;
    const std::uint32_t start =
        (rankPerm(cPos, 8, 2, kCorner2Blocks) * 9u) * kEdge3Entries +
        (rankPerm(ePos, 12, 3, kEdge3Blocks) * 8u);
    p.sb = buildCombinedDb(cTable, eTable, start);
    report(p.sb, "sb");
  }
  // CMLL pattern (4 U corners) with 4 U-rotation sources. bfsDb takes a
  // single start, so this multi-source seeding stays hand-rolled.
  {
    auto cTable = buildCornerMoveTable(4, kCorner4Blocks, kCorner4Rank, 81u, faceMoves, md);
    p.cmll.assign(kCmllEntries, 255u);
    std::vector<std::uint32_t> queue;
    queue.reserve(kCmllEntries);
    for (int k = 0; k < 4; ++k) {
      std::array<std::uint8_t, 6> pos{};
      for (int c = 0; c < 4; ++c) {
        for (int pp = 0; pp < 4; ++pp) {
          if (kUCornerRot[static_cast<std::size_t>(k)][static_cast<std::size_t>(pp)] ==
              static_cast<std::uint8_t>(c)) {
            pos[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(pp);
          }
        }
      }
      const std::uint32_t idx = rankPerm(pos, 8, 4, kCorner4Blocks) * 81u;
      if (p.cmll[idx] == 255u) {
        p.cmll[idx] = 0u;
        queue.push_back(idx);
      }
    }
    std::size_t head = 0;
    while (head < queue.size()) {
      const std::uint32_t idx = queue[head++];
      const std::uint8_t d = p.cmll[idx];
      for (int mi = 0; mi < 18; ++mi) {
        const std::uint32_t nidx =
            cTable[static_cast<std::size_t>(idx) * 18u + static_cast<std::size_t>(mi)];
        if (p.cmll[nidx] == 255u) {
          p.cmll[nidx] = static_cast<std::uint8_t>(d + 1u);
          queue.push_back(nidx);
        }
      }
    }
    report(p.cmll, "cmll");
  }
  // LSE abstract pattern (6 edges + centre rotation + U-corner rotation).
  {
    const std::vector<int> lseMoves = lseMoveIndices();
    auto eTable = buildLseEdgeMoveTable(lseMoves, md);
    // Delta centre/corner rotation per LSE move, in lseMoves order
    // (M, M', M2, U, U', U2).
    constexpr std::array<int, 6> dCentre = {1, 3, 2, 0, 0, 0};
    constexpr std::array<int, 6> dCorner = {0, 0, 0, 1, 3, 2};
    p.lse = bfsDb(kLseEntries, 0u, [&](std::uint32_t idx) {
      std::vector<std::uint32_t> out;
      out.reserve(6);
      const int cornerRot = static_cast<int>(idx % 4u);
      const int centreRot = static_cast<int>((idx / 4u) % 4u);
      const std::uint32_t eIdx = idx / 16u;
      for (int mi = 0; mi < 6; ++mi) {
        const std::uint32_t ne =
            eTable[static_cast<std::size_t>(eIdx) * 6u + static_cast<std::size_t>(mi)];
        const int nCentre = (centreRot + dCentre[static_cast<std::size_t>(mi)]) % 4;
        const int nCorner = (cornerRot + dCorner[static_cast<std::size_t>(mi)]) % 4;
        out.push_back(((ne * 4u + static_cast<std::uint32_t>(nCentre)) * 4u +
                       static_cast<std::uint32_t>(nCorner)));
      }
      return out;
    });
    report(p.lse, "lse");
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

// pdbLookup comes from core/pdb.hpp (see `using` near the includes).

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

// ---------------------------------------------------------------------------
// Stage search wired to the shared IDA* searcher (core/searcher.hpp).
// Heuristic = max of the stage's subproblem PDBs (admissible). Per-stage
// residues (move table, stage predicate, PDB wiring) stay here; only the
// search control flow is shared.
// ---------------------------------------------------------------------------
enum class StageKind { FirstBlock, SecondBlock, Cmll, Lse };

struct StageApply {
  const MoveData* md = nullptr;
  CubeState operator()(const CubeState& s, int moveIdx) const {
    return fastApply(s, moveIdx, *md);
  }
};

struct StageGoal {
  StageKind kind = StageKind::FirstBlock;
  bool operator()(const CubeState& s) const {
    if (kind == StageKind::FirstBlock) return is_first_block_solved(s);
    if (kind == StageKind::SecondBlock) return is_second_block_solved(s);
    if (kind == StageKind::Cmll) return is_cmll_solved(s);
    return is_lse_solved(s);
  }
};

struct StageHeuristic {
  StageKind kind = StageKind::FirstBlock;
  const Pdbs* pdbs = nullptr;
  int operator()(const CubeState& s) const {
    int h = 0;
    if (kind == StageKind::FirstBlock) {
      h = (std::max)(h, pdbLookup(pdbs->fb, fbCornerIdx(s) * kEdge3Entries + fbEdgeIdx(s)));
    } else if (kind == StageKind::SecondBlock) {
      h = (std::max)(h, pdbLookup(pdbs->fb, fbCornerIdx(s) * kEdge3Entries + fbEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->sb, sbCornerIdx(s) * kEdge3Entries + sbEdgeIdx(s)));
    } else if (kind == StageKind::Cmll) {
      h = (std::max)(h, pdbLookup(pdbs->fb, fbCornerIdx(s) * kEdge3Entries + fbEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->sb, sbCornerIdx(s) * kEdge3Entries + sbEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->cmll, cmllIdx(s)));
    } else {
      const int cRot = centreRotOf(s);
      const int kRot = cornerRotOf(s);
      if (cRot >= 0 && kRot >= 0) {
        const std::uint32_t aidx =
            ((lseEdgeIdx(s) * 4u + static_cast<std::uint32_t>(cRot)) * 4u +
             static_cast<std::uint32_t>(kRot));
        h = (std::max)(h, pdbLookup(pdbs->lse, aidx));
      }
    }
    return h;
  }
};

using RouxSearcher = IdaSearcher<CubeState, StageApply, StageGoal, StageHeuristic>;

// Thin stage runner over the shared searcher. Keeps the historical Timeout
// contract when the initial bound alone exceeds the stage limit (the shared
// run() reports depth exhaustion as Unsolvable instead), and resolves an
// already-satisfied stage ahead of any deadline/token check, as before.
std::expected<std::vector<int>, SolveError> runStage(
    const CubeState& start, StageKind kind, const std::vector<int>& allowed,
    int stageLimit, bool hasDeadline,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token externalToken) {
  const MoveData& md = getMoveData();
  const Pdbs& pdbs = getPdbs();
  if (StageGoal{kind}(start)) return std::vector<int>{};
  const StageHeuristic heuristic{kind, &pdbs};
  if (heuristic(start) > stageLimit) {
    return std::unexpected(SolveError::Timeout);
  }
  RouxSearcher searcher(&allowed, StageApply{&md}, StageGoal{kind}, heuristic);
  if (hasDeadline) searcher.setDeadline(deadline);
  searcher.setStopToken(externalToken);
  return searcher.run(start, stageLimit);
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
  const std::vector<int> lseIdx = lseMoveIndices();

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
    std::cout << std::format("roux stage {}: {} moves, {:.1f} ms\n", nm, tk.size(), ms);
    stageT0 = now;
  };

  for (const std::string& name : stage_names()) {
    StageKind kind = StageKind::Lse;
    bool (*check)(const CubeState&) = &is_lse_solved;
    const std::vector<int>& allowedIdx = (name == "lse") ? lseIdx : faceIdx;
    if (name == "first_block") {
      kind = StageKind::FirstBlock;
      check = &is_first_block_solved;
    } else if (name == "second_block") {
      kind = StageKind::SecondBlock;
      check = &is_second_block_solved;
    } else if (name == "cmll") {
      kind = StageKind::Cmll;
      check = &is_cmll_solved;
    }
    auto r = runStage(cur, kind, allowedIdx, stageLimit, hasDeadline, deadline,
                      options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage(name)) || !check(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{name, toks});
    logStage(name, toks);
  }
  if (!is_solved(cur)) return std::unexpected(SolveError::Unsolvable);
  return sol;
}

}  // namespace rubik::roux
