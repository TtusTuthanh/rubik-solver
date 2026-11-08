#include "solvers/zz/solver.hpp"

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

namespace rubik::zz {

// ---------------------------------------------------------------------------
// Corners 0..7, edges 0..11 as in core/cube.hpp.
// Left block: corners 5,6 (DLF,DBL), edges 6,9,10 (DL,FL,BL) + L centre.
// Right block: corners 4,7 (DFR,DRB), edges 4,8,11 (DR,FR,BR) + R centre.
// EOLine edges: DF(5), DB(7). U layer: corners/edges 0..3.
// ---------------------------------------------------------------------------

bool is_eoline_solved(const CubeState& state) {
  for (int i = 0; i < 12; ++i) {
    if (state.eo[static_cast<std::size_t>(i)] != 0u) return false;
  }
  if (state.ep[5] != 5u) return false;  // DF
  if (state.ep[7] != 7u) return false;  // DB
  return true;
}

bool is_eo_line_solved(const CubeState& state) {
  return is_eoline_solved(state);
}

bool is_eol_solved(const CubeState& state) {
  return is_eoline_solved(state);
}

bool is_zzf2l_left_solved(const CubeState& state) {
  if (state.cp[5] != 5u || state.co[5] != 0u) return false;   // DLF
  if (state.cp[6] != 6u || state.co[6] != 0u) return false;   // DBL
  if (state.ep[6] != 6u || state.eo[6] != 0u) return false;   // DL
  if (state.ep[9] != 9u || state.eo[9] != 0u) return false;   // FL
  if (state.ep[10] != 10u || state.eo[10] != 0u) return false;  // BL
  return true;
}

bool is_f2l_left_solved(const CubeState& state) {
  return is_zzf2l_left_solved(state);
}

bool is_left_block_solved(const CubeState& state) {
  return is_zzf2l_left_solved(state);
}

bool is_zzf2l_right_solved(const CubeState& state) {
  if (!is_zzf2l_left_solved(state)) return false;
  if (state.cp[4] != 4u || state.co[4] != 0u) return false;   // DFR
  if (state.cp[7] != 7u || state.co[7] != 0u) return false;   // DRB
  if (state.ep[4] != 4u || state.eo[4] != 0u) return false;   // DR
  if (state.ep[8] != 8u || state.eo[8] != 0u) return false;   // FR
  if (state.ep[11] != 11u || state.eo[11] != 0u) return false;  // BR
  return true;
}

bool is_f2l_right_solved(const CubeState& state) {
  return is_zzf2l_right_solved(state);
}

bool is_right_block_solved(const CubeState& state) {
  return is_zzf2l_right_solved(state);
}

bool is_zzf2l_solved(const CubeState& state) {
  if (!is_eoline_solved(state)) return false;
  return is_zzf2l_right_solved(state);
}

bool is_f2l_solved(const CubeState& state) { return is_zzf2l_solved(state); }

bool is_coll_solved(const CubeState& state) {
  if (!is_zzf2l_solved(state)) return false;
  for (int i = 0; i < 4; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (state.cp[ii] != static_cast<std::uint8_t>(i)) return false;
    if (state.co[ii] != 0u) return false;
  }
  return true;
}

bool is_eppl_solved(const CubeState& state) { return is_solved(state); }

bool is_epll_solved(const CubeState& state) { return is_eppl_solved(state); }

bool is_last_layer_solved(const CubeState& state) {
  return is_eppl_solved(state);
}

std::vector<std::string> stage_names() {
  return {"eoline", "zzf2l_left", "zzf2l_right", "coll", "eppl"};
}

std::vector<std::string> eoline_allowed_moves() {
  std::vector<std::string> out;
  for (const Move& m : all_face_moves()) out.push_back(m.token);
  return out;
}

std::vector<std::string> zzf2l_allowed_moves() {
  std::vector<std::string> out;
  for (const Move& m : all_face_moves()) {
    if (!m.token.empty() && (m.token[0] == 'R' || m.token[0] == 'L' ||
                              m.token[0] == 'U')) {
      out.push_back(m.token);
    }
  }
  return out;
}

std::vector<std::string> zzf2l_left_allowed_moves() {
  return zzf2l_allowed_moves();
}

std::vector<std::string> zzf2l_right_allowed_moves() {
  return zzf2l_allowed_moves();
}

std::vector<std::string> coll_allowed_moves() { return zzf2l_allowed_moves(); }

std::vector<std::string> eppl_allowed_moves() { return zzf2l_allowed_moves(); }

std::vector<std::string> allowed_moves_for_stage(const std::string& stage_name) {
  if (stage_name == "eoline" || stage_name == "eo_line" ||
      stage_name == "eol") {
    return eoline_allowed_moves();
  }
  if (stage_name == "zzf2l_left" || stage_name == "zzf2l" ||
      stage_name == "f2l_left") {
    return zzf2l_left_allowed_moves();
  }
  if (stage_name == "zzf2l_right" || stage_name == "f2l_right") {
    return zzf2l_right_allowed_moves();
  }
  if (stage_name == "coll") return coll_allowed_moves();
  if (stage_name == "eppl" || stage_name == "epll") return eppl_allowed_moves();
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
constexpr std::array<std::uint32_t, 6> kEdge4Blocks = {990, 90, 9, 1, 0, 0};
constexpr std::array<std::uint32_t, 6> kEdge2Blocks = {11, 1, 0, 0, 0, 0};
constexpr std::uint32_t kCorner2Rank = 56u;
constexpr std::uint32_t kEdge3Rank = 1320u;
constexpr std::uint32_t kCorner4Rank = 1680u;
constexpr std::uint32_t kEdge4Rank = 11880u;
constexpr std::uint32_t kEdge2Rank = 132u;
constexpr std::uint32_t kCorner2Entries = 504u;     // 56*9
constexpr std::uint32_t kEdge3Entries = 10560u;     // 1320*8
constexpr std::uint32_t kBlockEntries = 5322240u;   // 504*10560
constexpr std::uint32_t kCollEntries = 136080u;     // 1680*81
constexpr std::uint32_t kUEdgeEntries = 190080u;    // 11880*16
constexpr std::uint32_t kEoEntries = 2048u;         // 2^11
constexpr std::uint32_t kEolineEntries = 270336u;   // 2048*132

// Cubie lists in pattern order.
constexpr std::array<int, 2> kLeftCorners = {5, 6};
constexpr std::array<int, 3> kLeftEdges = {6, 9, 10};
constexpr std::array<int, 2> kRightCorners = {4, 7};
constexpr std::array<int, 3> kRightEdges = {4, 8, 11};
constexpr std::array<int, 4> kUCorners = {0, 1, 2, 3};
constexpr std::array<int, 4> kUEdges = {0, 1, 2, 3};

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
  return n;
}

// ---------------------------------------------------------------------------
// Pattern indices from a cubie state.
// ---------------------------------------------------------------------------
int findCornerPos(const CubeState& s, int cubie) {
  for (int p = 0; p < 8; ++p) {
    if (s.cp[static_cast<std::size_t>(p)] == static_cast<std::uint8_t>(cubie)) {
      return p;
    }
  }
  return -1;
}

int findEdgePos(const CubeState& s, int cubie) {
  for (int p = 0; p < 12; ++p) {
    if (s.ep[static_cast<std::size_t>(p)] == static_cast<std::uint8_t>(cubie)) {
      return p;
    }
  }
  return -1;
}

std::uint32_t cornerPatternIdx(const CubeState& s,
                               const std::array<int, 4>& cubies, int k,
                               const std::array<std::uint32_t, 6>& blocks) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < k; ++d) {
    const int cubie = cubies[static_cast<std::size_t>(d)];
    const int at = findCornerPos(s, cubie);
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] = s.co[static_cast<std::size_t>(at)];
  }
  const std::uint32_t orientEntries =
      (k == 2) ? 9u : 81u;
  return rankPerm(pos, 8, k, blocks) * orientEntries + cornerOrientIdx(ori, k);
}

std::uint32_t edgePatternIdx(const CubeState& s,
                             const std::array<int, 4>& cubies, int k,
                             const std::array<std::uint32_t, 6>& blocks) {
  std::array<std::uint8_t, 6> pos{};
  std::array<std::uint8_t, 6> ori{};
  for (int d = 0; d < k; ++d) {
    const int cubie = cubies[static_cast<std::size_t>(d)];
    const int at = findEdgePos(s, cubie);
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] = s.eo[static_cast<std::size_t>(at)];
  }
  const std::uint32_t orientEntries = (k == 2) ? 4u : ((k == 3) ? 8u : 16u);
  return rankPerm(pos, 12, k, blocks) * orientEntries + edgeOrientBits(ori, k);
}

std::uint32_t leftCornerIdx(const CubeState& s) {
  std::array<int, 4> c = {kLeftCorners[0], kLeftCorners[1], 0, 0};
  return cornerPatternIdx(s, c, 2, kCorner2Blocks);
}

std::uint32_t leftEdgeIdx(const CubeState& s) {
  std::array<int, 4> c = {kLeftEdges[0], kLeftEdges[1], kLeftEdges[2], 0};
  return edgePatternIdx(s, c, 3, kEdge3Blocks);
}

std::uint32_t rightCornerIdx(const CubeState& s) {
  std::array<int, 4> c = {kRightCorners[0], kRightCorners[1], 0, 0};
  return cornerPatternIdx(s, c, 2, kCorner2Blocks);
}

std::uint32_t rightEdgeIdx(const CubeState& s) {
  std::array<int, 4> c = {kRightEdges[0], kRightEdges[1], kRightEdges[2], 0};
  return edgePatternIdx(s, c, 3, kEdge3Blocks);
}

std::uint32_t collIdx(const CubeState& s) {
  return cornerPatternIdx(s, kUCorners, 4, kCorner4Blocks);
}

std::uint32_t uEdgeIdx(const CubeState& s) {
  return edgePatternIdx(s, kUEdges, 4, kEdge4Blocks);
}

std::uint32_t permIdx(const CubeState& s) {
  std::array<std::uint8_t, 6> cPos{};
  std::array<std::uint8_t, 6> ePos{};
  for (int d = 0; d < 4; ++d) {
    const std::size_t dd = static_cast<std::size_t>(d);
    cPos[dd] = static_cast<std::uint8_t>(findCornerPos(s, kUCorners[dd]));
    ePos[dd] = static_cast<std::uint8_t>(findEdgePos(s, kUEdges[dd]));
  }
  return rankPerm(cPos, 8, 4, kCorner4Blocks) * kEdge4Rank +
         rankPerm(ePos, 12, 4, kEdge4Blocks);
}

std::uint32_t eolineIdx(const CubeState& s) {
  std::uint32_t bits = 0;
  for (int i = 0; i < 11; ++i) {
    if (s.eo[static_cast<std::size_t>(i)] != 0u) {
      bits |= (1u << static_cast<std::uint32_t>(i));
    }
  }
  std::array<std::uint8_t, 6> pos{};
  pos[0] = static_cast<std::uint8_t>(findEdgePos(s, 5));  // DF cubie
  pos[1] = static_cast<std::uint8_t>(findEdgePos(s, 7));  // DB cubie
  return bits * kEdge2Rank + rankPerm(pos, 12, 2, kEdge2Blocks);
}

// ---------------------------------------------------------------------------
// Pattern move tables and PDB BFS.
// ---------------------------------------------------------------------------
std::vector<int> faceMoveIndices() {
  std::vector<int> out;
  for (int i = 0; i < 18; ++i) out.push_back(i);
  return out;
}

std::vector<int> zzMoveIndices() {
  // U,U',U2,R,R',R2,L,L',L2 in MoveData index space.
  return {0, 1, 2, 3, 4, 5, 12, 13, 14};
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

std::vector<std::uint8_t> buildCombinedDb(
    const std::vector<std::uint32_t>& cornerTable,
    const std::vector<std::uint32_t>& edgeTable, std::uint32_t cornerEntries,
    std::uint32_t edgeEntries, std::uint32_t startIdx, std::size_t moveCount) {
  const std::uint32_t total = cornerEntries * edgeEntries;
  std::vector<std::uint8_t> dist(total, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(total);
  dist[startIdx] = 0u;
  queue.push_back(startIdx);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    const std::uint32_t cIdx = idx / edgeEntries;
    const std::uint32_t eIdx = idx % edgeEntries;
    for (std::size_t mi = 0; mi < moveCount; ++mi) {
      const std::uint32_t nc =
          cornerTable[static_cast<std::size_t>(cIdx) * moveCount + mi];
      const std::uint32_t ne =
          edgeTable[static_cast<std::size_t>(eIdx) * moveCount + mi];
      const std::uint32_t nidx = nc * edgeEntries + ne;
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
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
      const std::uint32_t nidx =
          table[static_cast<std::size_t>(idx) * moveCount + mi];
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildEolineDb(const MoveData& md) {
  const std::vector<int> moves = faceMoveIndices();
  std::vector<std::uint32_t> table(
      static_cast<std::size_t>(kEolineEntries) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < kEolineEntries; ++idx) {
    const std::uint32_t eoBits = idx / kEdge2Rank;
    const std::uint32_t dfRank = idx % kEdge2Rank;
    std::array<std::uint8_t, 12> eo{};
    int sum = 0;
    for (int i = 0; i < 11; ++i) {
      eo[static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>((eoBits >> static_cast<std::uint32_t>(i)) & 1u);
      sum += eo[static_cast<std::size_t>(i)];
    }
    eo[11] = static_cast<std::uint8_t>(sum % 2);
    std::array<std::uint8_t, 6> pos{};
    unrankPerm(dfRank, 12, 2, kEdge2Blocks, pos);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const int mv = moves[mi];
      const std::size_t mmi = static_cast<std::size_t>(mv);
      std::array<std::uint8_t, 12> neo{};
      for (int p = 0; p < 12; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        const std::uint8_t src = md.cmEp[mmi][pp];
        neo[pp] = static_cast<std::uint8_t>(eo[src] ^ md.cmEo[mmi][pp]);
      }
      std::uint32_t nbits = 0;
      for (int i = 0; i < 11; ++i) {
        if (neo[static_cast<std::size_t>(i)] != 0u) {
          nbits |= (1u << static_cast<std::uint32_t>(i));
        }
      }
      std::array<std::uint8_t, 6> npos{};
      npos[0] = md.destE[mmi][pos[0]];
      npos[1] = md.destE[mmi][pos[1]];
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          nbits * kEdge2Rank + rankPerm(npos, 12, 2, kEdge2Blocks);
    }
  }
  std::array<std::uint8_t, 6> goalPos{};
  goalPos[0] = 5;
  goalPos[1] = 7;
  const std::uint32_t start = rankPerm(goalPos, 12, 2, kEdge2Blocks);
  return buildSingleDb(table, kEolineEntries, start, moves.size());
}

// Permutation-only move tables (U corners + U edges), for the EPLL
// combined-permutation PDB. Orientation is ignored here; the table is
// closed on permutation ranks alone.
std::vector<std::uint32_t> buildCornerPermTable(
    const std::vector<int>& moves, const MoveData& md) {
  std::vector<std::uint32_t> table(
      static_cast<std::size_t>(kCorner4Rank) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < kCorner4Rank; ++idx) {
    std::array<std::uint8_t, 6> pos{};
    unrankPerm(idx, 8, 4, kCorner4Blocks, pos);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const std::size_t mmi = static_cast<std::size_t>(moves[mi]);
      std::array<std::uint8_t, 6> npos{};
      for (int d = 0; d < 4; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        npos[dd] = md.destC[mmi][pos[dd]];
      }
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          rankPerm(npos, 8, 4, kCorner4Blocks);
    }
  }
  return table;
}

std::vector<std::uint32_t> buildEdgePermTable(
    const std::vector<int>& moves, const MoveData& md) {
  std::vector<std::uint32_t> table(
      static_cast<std::size_t>(kEdge4Rank) * moves.size(), 0u);
  for (std::uint32_t idx = 0; idx < kEdge4Rank; ++idx) {
    std::array<std::uint8_t, 6> pos{};
    unrankPerm(idx, 12, 4, kEdge4Blocks, pos);
    for (std::size_t mi = 0; mi < moves.size(); ++mi) {
      const std::size_t mmi = static_cast<std::size_t>(moves[mi]);
      std::array<std::uint8_t, 6> npos{};
      for (int d = 0; d < 4; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        npos[dd] = md.destE[mmi][pos[dd]];
      }
      table[static_cast<std::size_t>(idx) * moves.size() + mi] =
          rankPerm(npos, 12, 4, kEdge4Blocks);
    }
  }
  return table;
}

struct Pdbs {
  std::vector<std::uint8_t> eoline;
  std::vector<std::uint8_t> left;
  std::vector<std::uint8_t> right;
  std::vector<std::uint8_t> coll;
  std::vector<std::uint8_t> uedge;
  std::vector<std::uint8_t> perm;
};

Pdbs buildPdbs(const MoveData& md) {
  Pdbs p;
  const std::vector<int> zzMoves = zzMoveIndices();
  auto report = [](const std::vector<std::uint8_t>& db, const char* name) {
    std::size_t vis = 0;
    int mx = 0;
    for (std::uint8_t v : db) {
      if (v != 255u) ++vis;
      if (v != 255u && static_cast<int>(v) > mx) mx = static_cast<int>(v);
    }
    std::cout << std::format("zz pdb {}: {}/{} visited max {}\n", name, vis,
                             db.size(), mx);
  };

  p.eoline = buildEolineDb(md);
  report(p.eoline, "eoline");

  // Left block combined pattern (2 corners + 3 edges) under R/L/U.
  {
    auto cTable = buildCornerMoveTable(2, kCorner2Blocks, kCorner2Rank, 9u, zzMoves, md);
    auto eTable = buildEdgeMoveTable(3, kEdge3Blocks, kEdge3Rank, 8u, zzMoves, md);
    std::array<std::uint8_t, 6> cPos{};
    std::array<std::uint8_t, 6> ePos{};
    cPos[0] = 5;
    cPos[1] = 6;
    ePos[0] = 6;
    ePos[1] = 9;
    ePos[2] = 10;
    const std::uint32_t start =
        (rankPerm(cPos, 8, 2, kCorner2Blocks) * 9u) * kEdge3Entries +
        (rankPerm(ePos, 12, 3, kEdge3Blocks) * 8u);
    p.left = buildCombinedDb(cTable, eTable, kCorner2Entries, kEdge3Entries,
                             start, zzMoves.size());
    report(p.left, "left");
  }
  // Right block combined pattern (2 corners + 3 edges) under R/L/U.
  {
    auto cTable = buildCornerMoveTable(2, kCorner2Blocks, kCorner2Rank, 9u, zzMoves, md);
    auto eTable = buildEdgeMoveTable(3, kEdge3Blocks, kEdge3Rank, 8u, zzMoves, md);
    std::array<std::uint8_t, 6> cPos{};
    std::array<std::uint8_t, 6> ePos{};
    cPos[0] = 4;
    cPos[1] = 7;
    ePos[0] = 4;
    ePos[1] = 8;
    ePos[2] = 11;
    const std::uint32_t start =
        (rankPerm(cPos, 8, 2, kCorner2Blocks) * 9u) * kEdge3Entries +
        (rankPerm(ePos, 12, 3, kEdge3Blocks) * 8u);
    p.right = buildCombinedDb(cTable, eTable, kCorner2Entries, kEdge3Entries,
                              start, zzMoves.size());
    report(p.right, "right");
  }
  // COLL pattern (4 U corners, perm + orientation) under R/L/U.
  {
    auto cTable = buildCornerMoveTable(4, kCorner4Blocks, kCorner4Rank, 81u, zzMoves, md);
    p.coll = buildSingleDb(cTable, kCollEntries, 0u, zzMoves.size());
    report(p.coll, "coll");
  }
  // U-edge pattern (4 U edges, perm + orientation) under R/L/U.
  {
    auto eTable = buildEdgeMoveTable(4, kEdge4Blocks, kEdge4Rank, 16u, zzMoves, md);
    p.uedge = buildSingleDb(eTable, kUEdgeEntries, 0u, zzMoves.size());
    report(p.uedge, "uedge");
  }
  // Combined U-corner/U-edge permutation pattern under R/L/U (EPLL).
  {
    auto cTable = buildCornerPermTable(zzMoves, md);
    auto eTable = buildEdgePermTable(zzMoves, md);
    p.perm = buildCombinedDb(cTable, eTable, kCorner4Rank, kEdge4Rank, 0u,
                             zzMoves.size());
    report(p.perm, "perm");
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

// ---------------------------------------------------------------------------
// IDA* searcher. Heuristic = max of subproblem PDBs (admissible).
// ---------------------------------------------------------------------------
enum class StageKind { Eoline, Left, Right, Coll, Eppl };

struct Searcher {
  StageKind kind = StageKind::Eoline;
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
    int h = 0;
    if (kind == StageKind::Eoline) {
      h = (std::max)(h, pdbLookup(pdbs->eoline, eolineIdx(s)));
    } else if (kind == StageKind::Left) {
      h = (std::max)(h, pdbLookup(pdbs->eoline, eolineIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->left,
                                  leftCornerIdx(s) * kEdge3Entries + leftEdgeIdx(s)));
    } else if (kind == StageKind::Right) {
      h = (std::max)(h, pdbLookup(pdbs->eoline, eolineIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->left,
                                  leftCornerIdx(s) * kEdge3Entries + leftEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->right,
                                  rightCornerIdx(s) * kEdge3Entries + rightEdgeIdx(s)));
    } else if (kind == StageKind::Coll) {
      h = (std::max)(h, pdbLookup(pdbs->eoline, eolineIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->left,
                                  leftCornerIdx(s) * kEdge3Entries + leftEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->right,
                                  rightCornerIdx(s) * kEdge3Entries + rightEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->coll, collIdx(s)));
    } else {
      h = (std::max)(h, pdbLookup(pdbs->eoline, eolineIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->left,
                                  leftCornerIdx(s) * kEdge3Entries + leftEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->right,
                                  rightCornerIdx(s) * kEdge3Entries + rightEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->coll, collIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->uedge, uEdgeIdx(s)));
      h = (std::max)(h, pdbLookup(pdbs->perm, permIdx(s)));
    }
    return h;
  }

  bool goal(const CubeState& s) const {
    if (kind == StageKind::Eoline) return is_eoline_solved(s);
    if (kind == StageKind::Left) return is_zzf2l_left_solved(s);
    if (kind == StageKind::Right) return is_zzf2l_right_solved(s);
    if (kind == StageKind::Coll) return is_coll_solved(s);
    return is_eppl_solved(s);
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
  const std::vector<int> zzIdx = zzMoveIndices();

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
    std::cout << std::format("zz stage {}: {} moves, {:.1f} ms\n", nm, tk.size(), ms);
    stageT0 = now;
  };

  // Stage 1: EOLine.
  {
    auto r = runIda(cur, StageKind::Eoline, faceIdx, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("eoline")) ||
        !is_eoline_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"eoline", toks});
    logStage("eoline", toks);
  }
  // Stage 2: ZZF2L left block (R/L/U only).
  {
    auto r = runIda(cur, StageKind::Left, zzIdx, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("zzf2l_left")) ||
        !is_zzf2l_left_solved(next) || !is_eoline_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"zzf2l_left", toks});
    logStage("zzf2l_left", toks);
  }
  // Stage 3: ZZF2L right block (R/L/U only).
  {
    auto r = runIda(cur, StageKind::Right, zzIdx, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("zzf2l_right")) ||
        !is_zzf2l_right_solved(next) || !is_eoline_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"zzf2l_right", toks});
    logStage("zzf2l_right", toks);
  }
  // Stage 4: COLL (R/L/U only).
  {
    auto r = runIda(cur, StageKind::Coll, zzIdx, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("coll")) ||
        !is_coll_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"coll", toks});
    logStage("coll", toks);
  }
  // Stage 5: EPLL (R/L/U only).
  {
    auto r = runIda(cur, StageKind::Eppl, zzIdx, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("eppl")) ||
        !is_eppl_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"eppl", toks});
    logStage("eppl", toks);
  }
  if (!is_solved(cur)) return std::unexpected(SolveError::Unsolvable);
  return sol;
}

}  // namespace rubik::zz
