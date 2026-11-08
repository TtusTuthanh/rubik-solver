#include "solvers/cfop/solver.hpp"

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

namespace rubik::cfop {
namespace {

// ---------------------------------------------------------------------------
// Corners 0..7, edges 0..11 as in core/cube.hpp.
// Cross = D edges 4..7. U layer = corners/edges 0..3.
// F2L pairs: (4,8) DFR+FR, (5,9) DLF+FL, (6,10) DBL+BL, (7,11) DRB+BR.
// ---------------------------------------------------------------------------
constexpr std::array<int, 4> kCrossIds = {4, 5, 6, 7};
constexpr std::array<int, 4> kUIds = {0, 1, 2, 3};

constexpr std::uint32_t kEdgeEntries = 190080u;   // P(12,4)*16
constexpr std::uint32_t kCornerEntries = 136080u; // P(8,4)*81
constexpr std::uint32_t kPairEntries = 576u;      // 24*24

// ---------------------------------------------------------------------------
// Ranking helpers (factorial number system). Bijections onto [0, entries).
// ---------------------------------------------------------------------------
constexpr std::array<std::uint32_t, 4> kEdgeBlocks = {990u, 90u, 9u, 1u};
constexpr std::array<std::uint32_t, 4> kCornerBlocks = {210u, 30u, 5u, 1u};

std::uint32_t edgeRank4(const std::array<std::uint8_t, 4>& pos) {
  std::uint32_t rank = 0;
  bool used[12] = {};
  for (int d = 0; d < 4; ++d) {
    const int p = pos[static_cast<std::size_t>(d)];
    int cnt = 0;
    for (int v = 0; v < p; ++v) {
      if (!used[static_cast<std::size_t>(v)]) ++cnt;
    }
    rank += static_cast<std::uint32_t>(cnt) *
            kEdgeBlocks[static_cast<std::size_t>(d)];
    used[static_cast<std::size_t>(p)] = true;
  }
  return rank;
}

void edgeUnrank4(std::uint32_t rank,
                 std::array<std::uint8_t, 4>& pos) {
  bool used[12] = {};
  for (int d = 0; d < 4; ++d) {
    const std::uint32_t block = kEdgeBlocks[static_cast<std::size_t>(d)];
    const int k = static_cast<int>(rank / block);
    rank %= block;
    int seen = -1;
    int val = -1;
    for (int v = 0; v < 12; ++v) {
      if (!used[static_cast<std::size_t>(v)]) {
        ++seen;
        if (seen == k) {
          val = v;
          break;
        }
      }
    }
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(val);
    used[static_cast<std::size_t>(val)] = true;
  }
}

std::uint32_t cornerRank4(const std::array<std::uint8_t, 4>& pos) {
  std::uint32_t rank = 0;
  bool used[8] = {};
  for (int d = 0; d < 4; ++d) {
    const int p = pos[static_cast<std::size_t>(d)];
    int cnt = 0;
    for (int v = 0; v < p; ++v) {
      if (!used[static_cast<std::size_t>(v)]) ++cnt;
    }
    rank += static_cast<std::uint32_t>(cnt) *
            kCornerBlocks[static_cast<std::size_t>(d)];
    used[static_cast<std::size_t>(p)] = true;
  }
  return rank;
}

void cornerUnrank4(std::uint32_t rank,
                   std::array<std::uint8_t, 4>& pos) {
  bool used[8] = {};
  for (int d = 0; d < 4; ++d) {
    const std::uint32_t block = kCornerBlocks[static_cast<std::size_t>(d)];
    const int k = static_cast<int>(rank / block);
    rank %= block;
    int seen = -1;
    int val = -1;
    for (int v = 0; v < 8; ++v) {
      if (!used[static_cast<std::size_t>(v)]) {
        ++seen;
        if (seen == k) {
          val = v;
          break;
        }
      }
    }
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(val);
    used[static_cast<std::size_t>(val)] = true;
  }
}

std::uint32_t cornerOrientIdx(const std::array<std::uint8_t, 4>& ori) {
  return static_cast<std::uint32_t>(ori[0]) +
         static_cast<std::uint32_t>(ori[1]) * 3u +
         static_cast<std::uint32_t>(ori[2]) * 9u +
         static_cast<std::uint32_t>(ori[3]) * 27u;
}

void cornerOrientUnrank(std::uint32_t idx,
                        std::array<std::uint8_t, 4>& ori) {
  for (int d = 0; d < 4; ++d) {
    ori[static_cast<std::size_t>(d)] =
        static_cast<std::uint8_t>(idx % 3u);
    idx /= 3u;
  }
}

std::uint32_t edgeOrientBits(const std::array<std::uint8_t, 4>& ori) {
  std::uint32_t bits = 0;
  for (int d = 0; d < 4; ++d) {
    if (ori[static_cast<std::size_t>(d)] != 0u) {
      bits |= (1u << static_cast<std::uint32_t>(d));
    }
  }
  return bits;
}

void edgeOrientUnrank(std::uint32_t bits,
                      std::array<std::uint8_t, 4>& ori) {
  for (int d = 0; d < 4; ++d) {
    ori[static_cast<std::size_t>(d)] =
        static_cast<std::uint8_t>((bits >> static_cast<std::uint32_t>(d)) & 1u);
  }
}

// ---------------------------------------------------------------------------
// Move data derived from the frozen core (verified against rubik::apply in
// tests). cmCp/cmCo/cmEp/cmEo: new[p] = old[cm[p]] (+ orientation).
// destC/destE map old position -> new position; addC/addE are orientation
// deltas indexed by new position.
// ---------------------------------------------------------------------------
struct MoveData {
  std::array<std::array<std::uint8_t, 8>, 18> cmCp{};
  std::array<std::array<std::uint8_t, 8>, 18> cmCo{};
  std::array<std::array<std::uint8_t, 12>, 18> cmEp{};
  std::array<std::array<std::uint8_t, 12>, 18> cmEo{};
  std::array<std::string, 18> tokens{};
  std::array<std::array<std::uint8_t, 8>, 18> destC{};
  std::array<std::array<std::uint8_t, 8>, 18> addC{};
  std::array<std::array<std::uint8_t, 12>, 18> destE{};
  std::array<std::array<std::uint8_t, 12>, 18> addE{};
};

MoveData buildMoveData() {
  MoveData md;
  const std::vector<Move> all = all_face_moves();
  const CubeState solvedState = CubeState::solved();
  for (std::size_t i = 0; i < 18u; ++i) {
    const CubeState after = apply(solvedState, all[i]);
    md.tokens[i] = all[i].token;
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
  if (!gMoveData.has_value()) {
    gMoveData = buildMoveData();
  }
  return *gMoveData;
}

inline CubeState fastApply(const CubeState& s, int moveIdx,
                           const MoveData& md) {
  CubeState n = s;
  const auto& cp = md.cmCp[static_cast<std::size_t>(moveIdx)];
  const auto& co = md.cmCo[static_cast<std::size_t>(moveIdx)];
  const auto& ep = md.cmEp[static_cast<std::size_t>(moveIdx)];
  const auto& eo = md.cmEo[static_cast<std::size_t>(moveIdx)];
  for (std::size_t p = 0; p < 8u; ++p) {
    n.cp[p] = s.cp[cp[p]];
    n.co[p] = static_cast<std::uint8_t>(
        (static_cast<unsigned>(s.co[cp[p]]) + static_cast<unsigned>(co[p])) % 3u);
  }
  for (std::size_t p = 0; p < 12u; ++p) {
    n.ep[p] = s.ep[ep[p]];
    n.eo[p] = static_cast<std::uint8_t>(s.eo[ep[p]] ^ eo[p]);
  }
  return n;
}

// ---------------------------------------------------------------------------
// PDBs. BFS in abstract pattern space over the stage move set. Unreachable
// entries stay 255; heuristic lookup maps them to 0 (admissible).
// ---------------------------------------------------------------------------
std::vector<int> allowed18() {
  std::vector<int> out;
  for (int i = 0; i < 18; ++i) out.push_back(i);
  return out;
}

std::vector<int> allowed15() {
  std::vector<int> out;
  for (int i = 0; i < 18; ++i) {
    if (i >= 9 && i <= 11) continue;  // D, D', D2
    out.push_back(i);
  }
  return out;
}

std::vector<std::uint8_t> buildEdgeDb(
    const std::array<int, 4>& cubieIds, const std::vector<int>& allowed,
    bool multiSourceOriented, const MoveData& md) {
  std::vector<std::uint8_t> dist(kEdgeEntries, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(kEdgeEntries);
  if (multiSourceOriented) {
    for (std::uint32_t r = 0; r < 11880u; ++r) {
      const std::uint32_t idx = r * 16u;
      dist[idx] = 0u;
      queue.push_back(idx);
    }
  } else {
    std::array<std::uint8_t, 4> pos{};
    for (int d = 0; d < 4; ++d) {
      pos[static_cast<std::size_t>(d)] =
          static_cast<std::uint8_t>(cubieIds[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t idx = edgeRank4(pos) * 16u;
    dist[idx] = 0u;
    queue.push_back(idx);
  }
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    std::array<std::uint8_t, 4> pos{};
    std::array<std::uint8_t, 4> ori{};
    edgeUnrank4(idx / 16u, pos);
    edgeOrientUnrank(idx % 16u, ori);
    for (int mv : allowed) {
      const auto& dest = md.destE[static_cast<std::size_t>(mv)];
      const auto& add = md.addE[static_cast<std::size_t>(mv)];
      std::array<std::uint8_t, 4> npos{};
      std::array<std::uint8_t, 4> nori{};
      for (int dd = 0; dd < 4; ++dd) {
        const std::size_t ddd = static_cast<std::size_t>(dd);
        const std::uint8_t np = dest[pos[ddd]];
        npos[ddd] = np;
        nori[ddd] = static_cast<std::uint8_t>(ori[ddd] ^ add[np]);
      }
      const std::uint32_t nidx = edgeRank4(npos) * 16u + edgeOrientBits(nori);
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildCornerDb(
    const std::array<int, 4>& cubieIds, const std::vector<int>& allowed,
    bool multiSourceOriented, const MoveData& md) {
  std::vector<std::uint8_t> dist(kCornerEntries, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(kCornerEntries);
  if (multiSourceOriented) {
    for (std::uint32_t r = 0; r < 1680u; ++r) {
      const std::uint32_t idx = r * 81u;
      dist[idx] = 0u;
      queue.push_back(idx);
    }
  } else {
    std::array<std::uint8_t, 4> pos{};
    for (int d = 0; d < 4; ++d) {
      pos[static_cast<std::size_t>(d)] =
          static_cast<std::uint8_t>(cubieIds[static_cast<std::size_t>(d)]);
    }
    const std::uint32_t idx = cornerRank4(pos) * 81u;
    dist[idx] = 0u;
    queue.push_back(idx);
  }
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    std::array<std::uint8_t, 4> pos{};
    std::array<std::uint8_t, 4> ori{};
    cornerUnrank4(idx / 81u, pos);
    cornerOrientUnrank(idx % 81u, ori);
    for (int mv : allowed) {
      const auto& dest = md.destC[static_cast<std::size_t>(mv)];
      const auto& add = md.addC[static_cast<std::size_t>(mv)];
      std::array<std::uint8_t, 4> npos{};
      std::array<std::uint8_t, 4> nori{};
      for (int dd = 0; dd < 4; ++dd) {
        const std::size_t ddd = static_cast<std::size_t>(dd);
        const std::uint8_t np = dest[pos[ddd]];
        npos[ddd] = np;
        nori[ddd] = static_cast<std::uint8_t>(
            (static_cast<unsigned>(ori[ddd]) +
             static_cast<unsigned>(add[np])) % 3u);
      }
      const std::uint32_t nidx =
          cornerRank4(npos) * 81u + cornerOrientIdx(nori);
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildPairDb(int cornerCubie, int edgeCubie,
                                      const std::vector<int>& allowed,
                                      const MoveData& md) {
  std::vector<std::uint8_t> dist(kPairEntries, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(kPairEntries);
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
    for (int mv : allowed) {
      const auto& destC = md.destC[static_cast<std::size_t>(mv)];
      const auto& addC = md.addC[static_cast<std::size_t>(mv)];
      const auto& destE = md.destE[static_cast<std::size_t>(mv)];
      const auto& addE = md.addE[static_cast<std::size_t>(mv)];
      const std::uint8_t ncPos = destC[cPos];
      const std::uint8_t ncOri = static_cast<std::uint8_t>(
          (static_cast<unsigned>(cOri) +
           static_cast<unsigned>(addC[ncPos])) % 3u);
      const std::uint8_t nePos = destE[ePos];
      const std::uint8_t neOri =
          static_cast<std::uint8_t>(eOri ^ addE[nePos]);
      const std::uint32_t nidx = static_cast<std::uint32_t>(
          ((static_cast<unsigned>(ncPos) * 3u +
            static_cast<unsigned>(ncOri)) * 24u) +
          (static_cast<unsigned>(nePos) * 2u +
           static_cast<unsigned>(neOri)));
      if (dist[nidx] == 255u) {
        dist[nidx] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(nidx);
      }
    }
  }
  return dist;
}

struct Pdbs {
  std::vector<std::uint8_t> cross18;
  std::vector<std::uint8_t> cross15;
  std::array<std::vector<std::uint8_t>, 4> pair18;
  std::array<std::vector<std::uint8_t>, 4> pair15;
  std::vector<std::uint8_t> ollC;
  std::vector<std::uint8_t> ollE;
  std::vector<std::uint8_t> pllC;
  std::vector<std::uint8_t> pllE;
  std::vector<std::uint8_t> oriC;
  std::vector<std::uint8_t> oriE;
  std::vector<std::uint8_t> oriBoth;
  std::vector<std::uint8_t> pllPerm;
};

// Kociemba-style per-position orientation tables (corner orientation 3^7,
// edge orientation 2^11). Transitions are closed on the orientation
// coordinates alone, so BFS gives exact orientation distances; used as
// admissible heuristics for OLL/PLL (full orientation is required there).
std::uint32_t cornerOriIndex(const std::array<std::uint8_t, 8>& co) {
  std::uint32_t idx = 0;
  std::uint32_t mult = 1;
  for (int i = 0; i < 7; ++i) {
    idx += static_cast<std::uint32_t>(co[static_cast<std::size_t>(i)]) * mult;
    mult *= 3u;
  }
  return idx;
}

void cornerOriUnrank(std::uint32_t idx,
                     std::array<std::uint8_t, 8>& co) {
  int sum = 0;
  for (int i = 0; i < 7; ++i) {
    co[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(idx % 3u);
    idx /= 3u;
    sum += co[static_cast<std::size_t>(i)];
  }
  co[7] = static_cast<std::uint8_t>((3 - (sum % 3)) % 3);
}

std::uint32_t edgeOriIndex(const std::array<std::uint8_t, 12>& eo) {
  std::uint32_t bits = 0;
  for (int i = 0; i < 11; ++i) {
    if (eo[static_cast<std::size_t>(i)] != 0u) {
      bits |= (1u << static_cast<std::uint32_t>(i));
    }
  }
  return bits;
}

void edgeOriUnrank(std::uint32_t bits,
                   std::array<std::uint8_t, 12>& eo) {
  int sum = 0;
  for (int i = 0; i < 11; ++i) {
    eo[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>((bits >> static_cast<std::uint32_t>(i)) & 1u);
    sum += eo[static_cast<std::size_t>(i)];
  }
  eo[11] = static_cast<std::uint8_t>(sum % 2);
}

std::vector<std::uint8_t> buildOriCornerDb(const std::vector<int>& allowed,
                                           const MoveData& md) {
  constexpr std::uint32_t N = 2187u;
  std::vector<std::uint32_t> moveTable(18u * N, 0u);
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
      moveTable[static_cast<std::size_t>(m) * N + r] = cornerOriIndex(nco);
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
    for (int mv : allowed) {
      const std::uint32_t succ =
          moveTable[static_cast<std::size_t>(mv) * N + idx];
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildOriEdgeDb(const std::vector<int>& allowed,
                                         const MoveData& md) {
  constexpr std::uint32_t N = 2048u;
  std::vector<std::uint32_t> moveTable(18u * N, 0u);
  for (int m = 0; m < 18; ++m) {
    for (std::uint32_t r = 0; r < N; ++r) {
      std::array<std::uint8_t, 12> eo{};
      edgeOriUnrank(r, eo);
      std::array<std::uint8_t, 12> neo{};
      for (int p = 0; p < 12; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        const std::uint8_t src = md.cmEp[static_cast<std::size_t>(m)][pp];
        neo[pp] = static_cast<std::uint8_t>(
            eo[src] ^ md.cmEo[static_cast<std::size_t>(m)][pp]);
      }
      moveTable[static_cast<std::size_t>(m) * N + r] = edgeOriIndex(neo);
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
    for (int mv : allowed) {
      const std::uint32_t succ =
          moveTable[static_cast<std::size_t>(mv) * N + idx];
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildOriBothDb(const std::vector<int>& allowed,
                                         const MoveData& md) {
  constexpr std::uint32_t kCN = 2187u;
  constexpr std::uint32_t kEN = 2048u;
  constexpr std::uint32_t kN = kCN * kEN;
  std::vector<std::uint16_t> cMove(18u * kCN, 0u);
  std::vector<std::uint16_t> eMove(18u * kEN, 0u);
  for (int m = 0; m < 18; ++m) {
    for (std::uint32_t r = 0; r < kCN; ++r) {
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
      cMove[static_cast<std::size_t>(m) * kCN + r] =
          static_cast<std::uint16_t>(cornerOriIndex(nco));
    }
    for (std::uint32_t r = 0; r < kEN; ++r) {
      std::array<std::uint8_t, 12> eo{};
      edgeOriUnrank(r, eo);
      std::array<std::uint8_t, 12> neo{};
      for (int p = 0; p < 12; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        const std::uint8_t src = md.cmEp[static_cast<std::size_t>(m)][pp];
        neo[pp] = static_cast<std::uint8_t>(
            eo[src] ^ md.cmEo[static_cast<std::size_t>(m)][pp]);
      }
      eMove[static_cast<std::size_t>(m) * kEN + r] =
          static_cast<std::uint16_t>(edgeOriIndex(neo));
    }
  }
  std::vector<std::uint8_t> dist(kN, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(kN);
  dist[0] = 0u;
  queue.push_back(0u);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    const std::uint32_t c = idx / kEN;
    const std::uint32_t e = idx % kEN;
    for (int mv : allowed) {
      const std::uint32_t nc =
          cMove[static_cast<std::size_t>(mv) * kCN + c];
      const std::uint32_t ne =
          eMove[static_cast<std::size_t>(mv) * kEN + e];
      const std::uint32_t succ = nc * kEN + ne;
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

std::vector<std::uint8_t> buildPllPermDb(const std::vector<int>& allowed,
                                        const MoveData& md) {
  constexpr std::uint32_t kCN = 1680u;
  constexpr std::uint32_t kEN = 11880u;
  constexpr std::uint32_t kN = kCN * kEN;  // 19,958,400
  std::vector<std::uint16_t> cMove(18u * kCN, 0u);
  std::vector<std::uint16_t> eMove(18u * kEN, 0u);
  for (int m = 0; m < 18; ++m) {
    for (std::uint32_t r = 0; r < kCN; ++r) {
      std::array<std::uint8_t, 4> pos{};
      cornerUnrank4(r, pos);
      std::array<std::uint8_t, 4> npos{};
      for (int d = 0; d < 4; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        npos[dd] = md.destC[static_cast<std::size_t>(m)][pos[dd]];
      }
      cMove[static_cast<std::size_t>(m) * kCN + r] =
          static_cast<std::uint16_t>(cornerRank4(npos));
    }
    for (std::uint32_t r = 0; r < kEN; ++r) {
      std::array<std::uint8_t, 4> pos{};
      edgeUnrank4(r, pos);
      std::array<std::uint8_t, 4> npos{};
      for (int d = 0; d < 4; ++d) {
        const std::size_t dd = static_cast<std::size_t>(d);
        npos[dd] = md.destE[static_cast<std::size_t>(m)][pos[dd]];
      }
      eMove[static_cast<std::size_t>(m) * kEN + r] =
          static_cast<std::uint16_t>(edgeRank4(npos));
    }
  }
  std::vector<std::uint8_t> dist(kN, 255u);
  std::vector<std::uint32_t> queue;
  queue.reserve(kN);
  dist[0] = 0u;
  queue.push_back(0u);
  std::size_t head = 0;
  while (head < queue.size()) {
    const std::uint32_t idx = queue[head++];
    const std::uint8_t d = dist[idx];
    const std::uint32_t cRank = idx / kEN;
    const std::uint32_t eRank = idx % kEN;
    for (int mv : allowed) {
      const std::uint32_t nc =
          cMove[static_cast<std::size_t>(mv) * kCN + cRank];
      const std::uint32_t ne =
          eMove[static_cast<std::size_t>(mv) * kEN + eRank];
      const std::uint32_t succ = nc * kEN + ne;
      if (dist[succ] == 255u) {
        dist[succ] = static_cast<std::uint8_t>(d + 1u);
        queue.push_back(succ);
      }
    }
  }
  return dist;
}

Pdbs buildPdbs(const MoveData& md) {
  Pdbs p;
  const std::vector<int> a18 = allowed18();
  const std::vector<int> a15 = allowed15();
  auto report = [](const std::vector<std::uint8_t>& db, const char* name) {
    std::size_t vis = 0;
    int mx = 0;
    for (std::uint8_t v : db) {
      if (v != 255u) ++vis;
      if (v != 255u && static_cast<int>(v) > mx) mx = static_cast<int>(v);
    }
    std::cout << std::format("pdb {}: {}/{} visited max {}\n", name, vis,
                             db.size(), mx);
  };
  p.cross18 = buildEdgeDb(kCrossIds, a18, false, md);
  report(p.cross18, "cross18");
  p.cross15 = buildEdgeDb(kCrossIds, a15, false, md);
  report(p.cross15, "cross15");
  for (int k = 0; k < 4; ++k) {
    const int cid = 4 + k;
    const int eid = 8 + k;
    p.pair18[static_cast<std::size_t>(k)] = buildPairDb(cid, eid, a18, md);
    p.pair15[static_cast<std::size_t>(k)] = buildPairDb(cid, eid, a15, md);
  }
  report(p.pair18[0], "pair18_0");
  report(p.pair15[0], "pair15_0");
  p.ollC = buildCornerDb(kUIds, a15, true, md);
  report(p.ollC, "ollC");
  p.ollE = buildEdgeDb(kUIds, a15, true, md);
  report(p.ollE, "ollE");
  p.pllC = buildCornerDb(kUIds, a15, false, md);
  report(p.pllC, "pllC");
  p.pllE = buildEdgeDb(kUIds, a15, false, md);
  report(p.pllE, "pllE");
  p.oriC = buildOriCornerDb(a15, md);
  report(p.oriC, "oriC");
  p.oriE = buildOriEdgeDb(a15, md);
  report(p.oriE, "oriE");
  p.oriBoth = buildOriBothDb(a15, md);
  report(p.oriBoth, "oriBoth");
  p.pllPerm = buildPllPermDb(a15, md);
  report(p.pllPerm, "pllPerm");
  return p;
}

std::mutex gPdbMutex;
std::optional<Pdbs> gPdbs;

const Pdbs& getPdbs() {
  const MoveData& md = getMoveData();
  std::lock_guard<std::mutex> lock(gPdbMutex);
  if (!gPdbs.has_value()) {
    gPdbs = buildPdbs(md);
  }
  return *gPdbs;
}

// ---------------------------------------------------------------------------
// Pattern indices from a cubie state.
// ---------------------------------------------------------------------------
std::uint32_t edgePatternIdx(const CubeState& s,
                             const std::array<int, 4>& ids) {
  std::array<std::uint8_t, 4> pos{};
  std::array<std::uint8_t, 4> ori{};
  for (int d = 0; d < 4; ++d) {
    const int cubie = ids[static_cast<std::size_t>(d)];
    int at = -1;
    for (int p = 0; p < 12; ++p) {
      if (s.ep[static_cast<std::size_t>(p)] == cubie) {
        at = p;
        break;
      }
    }
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] =
        s.eo[static_cast<std::size_t>(at)];
  }
  return edgeRank4(pos) * 16u + edgeOrientBits(ori);
}

std::uint32_t cornerPatternIdx(const CubeState& s,
                               const std::array<int, 4>& ids) {
  std::array<std::uint8_t, 4> pos{};
  std::array<std::uint8_t, 4> ori{};
  for (int d = 0; d < 4; ++d) {
    const int cubie = ids[static_cast<std::size_t>(d)];
    int at = -1;
    for (int p = 0; p < 8; ++p) {
      if (s.cp[static_cast<std::size_t>(p)] == cubie) {
        at = p;
        break;
      }
    }
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    ori[static_cast<std::size_t>(d)] =
        s.co[static_cast<std::size_t>(at)];
  }
  return cornerRank4(pos) * 81u + cornerOrientIdx(ori);
}

std::uint32_t pairPatternIdx(const CubeState& s, int cornerCubie,
                             int edgeCubie) {
  int cPos = -1;
  for (int p = 0; p < 8; ++p) {
    if (s.cp[static_cast<std::size_t>(p)] == cornerCubie) {
      cPos = p;
      break;
    }
  }
  int ePos = -1;
  for (int p = 0; p < 12; ++p) {
    if (s.ep[static_cast<std::size_t>(p)] == edgeCubie) {
      ePos = p;
      break;
    }
  }
  const unsigned cOri = s.co[static_cast<std::size_t>(cPos)];
  const unsigned eOri = s.eo[static_cast<std::size_t>(ePos)];
  return static_cast<std::uint32_t>(
      (((static_cast<unsigned>(cPos) * 3u + cOri) * 24u) +
       (static_cast<unsigned>(ePos) * 2u + eOri)));
}

inline int pdbLookup(const std::vector<std::uint8_t>& db,
                     std::uint32_t idx) {
  const std::uint8_t v = db[idx];
  return (v == 255u) ? 0 : static_cast<int>(v);
}

std::uint32_t pllPermIdx(const CubeState& s) {
  std::array<std::uint8_t, 4> cPos{};
  std::array<std::uint8_t, 4> ePos{};
  for (int d = 0; d < 4; ++d) {
    int at = -1;
    for (int p = 0; p < 8; ++p) {
      if (s.cp[static_cast<std::size_t>(p)] == kUIds[static_cast<std::size_t>(d)]) {
        at = p;
        break;
      }
    }
    cPos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(at);
    int eat = -1;
    for (int p = 0; p < 12; ++p) {
      if (s.ep[static_cast<std::size_t>(p)] == kUIds[static_cast<std::size_t>(d)]) {
        eat = p;
        break;
      }
    }
    ePos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(eat);
  }
  return cornerRank4(cPos) * 11880u + edgeRank4(ePos);
}

// ---------------------------------------------------------------------------
// Validity (same rules as the optimal solver: face moves keep centres fixed).
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
enum class StageKind { Cross, F2L, Oll, Pll, PllCorners, OllCorners };

struct Searcher {
  StageKind kind = StageKind::Cross;
  int pairIdx = 0;
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
    if (kind == StageKind::Cross) {
      h = (std::max)(h, pdbLookup(pdbs->cross18,
                                  edgePatternIdx(s, kCrossIds)));
    } else if (kind == StageKind::F2L) {
      h = (std::max)(h, pdbLookup(pdbs->cross18,
                                  edgePatternIdx(s, kCrossIds)));
      for (int j = 0; j <= pairIdx; ++j) {
        const int cid = 4 + j;
        const int eid = 8 + j;
        h = (std::max)(h, pdbLookup(pdbs->pair18[static_cast<std::size_t>(j)],
                                    pairPatternIdx(s, cid, eid)));
      }
    } else if (kind == StageKind::OllCorners) {
      h = (std::max)(h, pdbLookup(pdbs->cross15,
                                  edgePatternIdx(s, kCrossIds)));
      for (int j = 0; j < 4; ++j) {
        const int cid = 4 + j;
        const int eid = 8 + j;
        h = (std::max)(h, pdbLookup(pdbs->pair15[static_cast<std::size_t>(j)],
                                    pairPatternIdx(s, cid, eid)));
      }
      h = (std::max)(h, pdbLookup(pdbs->ollC, cornerPatternIdx(s, kUIds)));
    } else if (kind == StageKind::Oll) {
      h = (std::max)(h, pdbLookup(pdbs->cross15,
                                  edgePatternIdx(s, kCrossIds)));
      for (int j = 0; j < 4; ++j) {
        const int cid = 4 + j;
        const int eid = 8 + j;
        h = (std::max)(h, pdbLookup(pdbs->pair15[static_cast<std::size_t>(j)],
                                    pairPatternIdx(s, cid, eid)));
      }
      h = (std::max)(h, pdbLookup(pdbs->ollC, cornerPatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->ollE, edgePatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->oriC, cornerOriIndex(s.co)));
      h = (std::max)(h, pdbLookup(pdbs->oriE, edgeOriIndex(s.eo)));
      h = (std::max)(h, pdbLookup(pdbs->oriBoth,
                                 cornerOriIndex(s.co) * 2048u +
                                     edgeOriIndex(s.eo)));
    } else if (kind == StageKind::PllCorners) {
      h = (std::max)(h, pdbLookup(pdbs->cross15,
                                  edgePatternIdx(s, kCrossIds)));
      for (int j = 0; j < 4; ++j) {
        const int cid = 4 + j;
        const int eid = 8 + j;
        h = (std::max)(h, pdbLookup(pdbs->pair15[static_cast<std::size_t>(j)],
                                    pairPatternIdx(s, cid, eid)));
      }
      h = (std::max)(h, pdbLookup(pdbs->ollE, edgePatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->pllC, cornerPatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->oriC, cornerOriIndex(s.co)));
      h = (std::max)(h, pdbLookup(pdbs->oriE, edgeOriIndex(s.eo)));
      h = (std::max)(h, pdbLookup(pdbs->oriBoth,
                                 cornerOriIndex(s.co) * 2048u +
                                     edgeOriIndex(s.eo)));
    } else {
      h = (std::max)(h, pdbLookup(pdbs->cross15,
                                  edgePatternIdx(s, kCrossIds)));
      for (int j = 0; j < 4; ++j) {
        const int cid = 4 + j;
        const int eid = 8 + j;
        h = (std::max)(h, pdbLookup(pdbs->pair15[static_cast<std::size_t>(j)],
                                    pairPatternIdx(s, cid, eid)));
      }
      h = (std::max)(h, pdbLookup(pdbs->ollC, cornerPatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->ollE, edgePatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->pllC, cornerPatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->pllE, edgePatternIdx(s, kUIds)));
      h = (std::max)(h, pdbLookup(pdbs->oriC, cornerOriIndex(s.co)));
      h = (std::max)(h, pdbLookup(pdbs->oriE, edgeOriIndex(s.eo)));
      h = (std::max)(h, pdbLookup(pdbs->oriBoth,
                                 cornerOriIndex(s.co) * 2048u +
                                     edgeOriIndex(s.eo)));
      h = (std::max)(h, pdbLookup(pdbs->pllPerm, pllPermIdx(s)));
    }
    return h;
  }

  bool goal(const CubeState& s) const {
    if (kind == StageKind::Cross) return is_cross_solved(s);
    if (kind == StageKind::F2L) {
      if (!is_cross_solved(s)) return false;
      for (int j = 0; j <= pairIdx; ++j) {
        if (!is_f2l_pair_solved(s, j)) return false;
      }
      return true;
    }
    if (kind == StageKind::Oll) return is_oll_solved(s);
    if (kind == StageKind::OllCorners) {
      if (!is_f2l_solved(s)) return false;
      for (int i = 0; i < 4; ++i) {
        if (s.co[static_cast<std::size_t>(i)] != 0u) return false;
      }
      return true;
    }
    if (kind == StageKind::PllCorners) {
      if (!is_f2l_solved(s)) return false;
      for (int i = 0; i < 4; ++i) {
        const std::size_t ii = static_cast<std::size_t>(i);
        if (s.cp[ii] != static_cast<std::uint8_t>(i)) return false;
        if (s.co[ii] != 0u) return false;
      }
      for (int i = 0; i < 12; ++i) {
        if (s.eo[static_cast<std::size_t>(i)] != 0u) return false;
      }
      return true;
    }
    return is_pll_solved(s);
  }

  bool dfsOrdered(const CubeState& cur, int g, int prevFace, int bound) {
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
    struct Cand {
      int h = 0;
      int mvIdx = -1;
      CubeState next;
    };
    std::array<Cand, 18> cands;
    int n = 0;
    for (int mvIdx : *allowed) {
      const int face = mvIdx / 3;
      if (face == prevFace) continue;
      const CubeState next = fastApply(cur, mvIdx, *mv);
      const int h = heuristic(next);
      if (g + 1 + h > bound) continue;
      cands[static_cast<std::size_t>(n)].h = h;
      cands[static_cast<std::size_t>(n)].mvIdx = mvIdx;
      cands[static_cast<std::size_t>(n)].next = next;
      ++n;
    }
    std::sort(cands.begin(), cands.begin() + n,
              [](const Cand& a, const Cand& b) { return a.h < b.h; });
    for (int i = 0; i < n; ++i) {
      const int mvIdx = cands[static_cast<std::size_t>(i)].mvIdx;
      path.push_back(mvIdx);
      if (dfsOrdered(cands[static_cast<std::size_t>(i)].next, g + 1,
                     mvIdx / 3, bound)) {
        return true;
      }
      path.pop_back();
      if (abortKind != 0) return true;
    }
    return false;
  }

  bool dfs(const CubeState& cur, int g, int prevFace, int bound) {
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
      const int face = mvIdx / 3;
      if (face == prevFace) continue;
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
      if (dfs(cands[static_cast<std::size_t>(i)].next, g + 1, mvIdx / 3,
              bound)) {
        return true;
      }
      path.pop_back();
      if (abortKind != 0) return true;
    }
    return false;
  }
};

std::expected<std::vector<int>, SolveError> runIda(
    const CubeState& start, StageKind kind, int pairIdx,
    const std::vector<int>& allowed, int stageLimit, bool hasDeadline,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token externalToken) {
  const MoveData& md = getMoveData();
  const Pdbs& pdbs = getPdbs();
  Searcher searcher;
  searcher.kind = kind;
  searcher.pairIdx = pairIdx;
  searcher.allowed = &allowed;
  searcher.pdbs = &pdbs;
  searcher.mv = &md;
  searcher.hasDeadline = hasDeadline;
  searcher.deadline = deadline;
  searcher.externalToken = externalToken;
  if (searcher.goal(start)) {
    return std::vector<int>{};
  }
  int bound = searcher.heuristic(start);
  while (true) {
    if (bound > stageLimit) {
      return std::unexpected(SolveError::Timeout);
    }
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
    if (searcher.found) {
      return searcher.solution;
    }
    if (searcher.abortKind == 2) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (searcher.abortKind == 1) {
      return std::unexpected(SolveError::Timeout);
    }
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

std::vector<std::string> tokensFor(const std::vector<int>& idxs,
                                   const MoveData& md) {
  std::vector<std::string> out;
  out.reserve(idxs.size());
  for (int i : idxs) {
    out.push_back(md.tokens[static_cast<std::size_t>(i)]);
  }
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

std::vector<std::vector<int>> parsePllTable(const MoveData& md);

std::vector<std::vector<int>> parsePllTable(const MoveData& md) {
  std::vector<std::vector<int>> out;
  for (const std::string& alg : pll_algorithms()) {
    auto parsed = parse_moves(alg);
    if (!parsed) continue;
    std::vector<int> idxs;
    bool bad = false;
    for (const Move& m : *parsed) {
      int found = -1;
      for (int i = 0; i < 18; ++i) {
        if (md.tokens[static_cast<std::size_t>(i)] == m.token) {
          found = i;
          break;
        }
      }
      if (found < 0) {
        bad = true;
        break;
      }
      idxs.push_back(found);
    }
    if (!bad) out.push_back(std::move(idxs));
  }
  return out;
}

// Try table algorithms with U pre/post rotations. Returns the first
// sequence (move indices) within stageLimit that solves the cube,
// or nullopt when none fits. Simulation-verified via fastApply + is_solved.
std::optional<std::vector<int>> tryPllTable(
    const CubeState& start, const MoveData& md, int stageLimit) {
  if (is_solved(start)) return std::vector<int>{};
  const std::vector<std::vector<int>> table = parsePllTable(md);
  int uIdx = -1;
  for (int i = 0; i < 18; ++i) {
    if (md.tokens[static_cast<std::size_t>(i)] == "U") {
      uIdx = i;
      break;
    }
  }
  if (uIdx < 0) return std::nullopt;
  const std::vector<std::vector<int>> prePost = {
      {}, {uIdx}, {1}, {2}};  // U (0), U' (1), U2 (2) in all_face_moves order
  for (const auto& only : prePost) {
    if (static_cast<int>(only.size()) > stageLimit) continue;
    CubeState cur = start;
    for (int mv : only) cur = fastApply(cur, mv, md);
    if (is_solved(cur)) return only;
  }
  for (const auto& alg : table) {
    for (const auto& pre : prePost) {
      for (const auto& post : prePost) {
        const std::size_t len = pre.size() + alg.size() + post.size();
        if (static_cast<int>(len) > stageLimit) continue;
        CubeState cur = start;
        for (int mv : pre) cur = fastApply(cur, mv, md);
        for (int mv : alg) cur = fastApply(cur, mv, md);
        for (int mv : post) cur = fastApply(cur, mv, md);
        if (!is_solved(cur)) continue;
        std::vector<int> out;
        out.reserve(len);
        for (int mv : pre) out.push_back(mv);
        for (int mv : alg) out.push_back(mv);
        for (int mv : post) out.push_back(mv);
        return out;
      }
    }
  }
  return std::nullopt;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public predicates and move sets.
// ---------------------------------------------------------------------------
bool is_cross_solved(const CubeState& state) {
  for (int i = 0; i < 4; ++i) {
    const std::size_t p = static_cast<std::size_t>(4 + i);
    if (state.ep[p] != static_cast<std::uint8_t>(4 + i)) return false;
    if (state.eo[p] != 0u) return false;
  }
  return true;
}

bool is_f2l_pair_solved(const CubeState& state, int pairIndex) {
  if (pairIndex < 0 || pairIndex > 3) return false;
  const std::size_t cPos = static_cast<std::size_t>(4 + pairIndex);
  const std::size_t ePos = static_cast<std::size_t>(8 + pairIndex);
  if (state.cp[cPos] != static_cast<std::uint8_t>(4 + pairIndex)) {
    return false;
  }
  if (state.co[cPos] != 0u) return false;
  if (state.ep[ePos] != static_cast<std::uint8_t>(8 + pairIndex)) {
    return false;
  }
  if (state.eo[ePos] != 0u) return false;
  return true;
}

bool is_f2l_solved(const CubeState& state) {
  if (!is_cross_solved(state)) return false;
  for (int i = 0; i < 4; ++i) {
    if (!is_f2l_pair_solved(state, i)) return false;
  }
  return true;
}

bool is_oll_solved(const CubeState& state) {
  if (!is_f2l_solved(state)) return false;
  for (int i = 0; i < 8; ++i) {
    if (state.co[static_cast<std::size_t>(i)] != 0u) return false;
  }
  for (int i = 0; i < 12; ++i) {
    if (state.eo[static_cast<std::size_t>(i)] != 0u) return false;
  }
  return true;
}

bool is_pll_solved(const CubeState& state) { return is_solved(state); }

std::vector<std::string> stage_names() {
  return {"cross", "f2l_1", "f2l_2", "f2l_3", "f2l_4", "oll", "pll"};
}

std::vector<std::string> cross_allowed_moves() {
  std::vector<std::string> out;
  for (const Move& m : all_face_moves()) out.push_back(m.token);
  return out;
}

std::vector<std::string> f2l_allowed_moves() {
  return cross_allowed_moves();
}

std::vector<std::string> oll_allowed_moves() {
  std::vector<std::string> out;
  for (const Move& m : all_face_moves()) {
    if (!m.token.empty() && m.token[0] == 'D') continue;
    out.push_back(m.token);
  }
  return out;
}

std::vector<std::string> pll_allowed_moves() { return cross_allowed_moves(); }

std::vector<std::string> pll_algorithms() {
  return {
      "R2 F2 B2 L2 D' R2 F2 B2 L2",
      "R U R' U R' U' R2 U' R' U R' U R",
      "R' U R' U' R' U' R' U R U R2",
      "R' U' R U' R U R U' R' U R U R2 U' R'",
      "R2 F2 R' B' R F2 R' B R'",
      "R' D R U2 R' D' R U R' D R U R' D' R",
      "F R B R' F' R L F L' B' L F' R' L'",
      "R' U R U' R2 F' U' F U R F R' F' R2 U'",
      "R U R' U' R' U F R U R U' R' F' U R' U2 R",
      "R' U' R U D' R2 U R' U R U' R U' R2 D",
      "L' U' L U L U' F' L' U' L' U L F U' L U2 L'",
      "L U L' B2 D' R U' R' U R' D B2",
      "L2 D L D' L B2 R' U R B2",
      "R2 D' R' D R' B2 L U' L' B2",
      "R' U R2 B2 U R' B2 R U' B2 R2 U' R",
      "L U' L2 B2 U' L B2 L' U B2 L2 U L'",
      "R U2 R' U2 R B' R' U' R U R B R2 U",
      "R' U2 R U2 R' F R U R' U' R' F' R2",
      "R U R' U' R' F R2 U' R' U' R U R' F'",
      "R' U R' U' B' D B' D' B2 R' B' R B R",
      "R2 U' R2 U' R2 U F U F' R2 F U' F'",
  };
}

std::vector<std::string> allowed_moves_for_stage(
    const std::string& stage_name) {
  if (stage_name == "cross") return cross_allowed_moves();
  if (stage_name == "f2l_1" || stage_name == "f2l_2" ||
      stage_name == "f2l_3" || stage_name == "f2l_4") {
    return f2l_allowed_moves();
  }
  if (stage_name == "oll") return oll_allowed_moves();
  if (stage_name == "pll") return pll_allowed_moves();
  return {};
}

std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options) {
  if (options.stop_token.stop_requested()) {
    return std::unexpected(SolveError::Cancelled);
  }
  if (!isValidCubieState(state)) {
    return std::unexpected(SolveError::Unsolvable);
  }
  const MoveData& md = getMoveData();
  const std::vector<int> a18 = allowed18();
  const std::vector<int> a15 = allowed15();

  const auto t0 = std::chrono::steady_clock::now();
  const bool hasDeadline = options.timeout_ms > 0;
  const auto deadline =
      hasDeadline
          ? t0 + std::chrono::milliseconds(options.timeout_ms)
          : t0;
  int stageLimit = options.max_depth;
  if (stageLimit < 0) stageLimit = 0;

  CubeState cur = state;
  Solution sol;
  auto stageT0 = std::chrono::steady_clock::now();
  auto logStage = [&](const std::string& nm, const std::vector<std::string>& tk) {
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - stageT0).count();
    std::cout << std::format("cfop stage {}: {} moves, {:.1f} ms\n", nm, tk.size(), ms);
    stageT0 = now;
  };

  // Run stages explicitly so each predicate is checked with its own helper.
  {
    auto r = runIda(cur, StageKind::Cross, 0, a18, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("cross")) ||
        !is_cross_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"cross", toks});
    logStage("cross", toks);
  }
  for (int k = 0; k < 4; ++k) {
    const std::string name = std::format("f2l_{}", k + 1);
    auto r = runIda(cur, StageKind::F2L, k, a18, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    bool predOk = is_cross_solved(next);
    for (int j = 0; j <= k; ++j) {
      if (!is_f2l_pair_solved(next, j)) predOk = false;
    }
    if (!movesAllowed(toks, allowed_moves_for_stage(name)) || !predOk) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{name, toks});
    logStage(name, toks);
  }
  {
    auto r = runIda(cur, StageKind::Oll, 0, a15, stageLimit, hasDeadline,
                    deadline, options.stop_token);
    if (!r) return std::unexpected(r.error());
    const std::vector<int> idxs = *r;
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("oll")) ||
        !is_oll_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"oll", toks});
    logStage("oll", toks);
  }
  {
    // PLL: simulation-verified algorithm table with U pre/post rotations
    // first (O(1)), per-stage IDA* as fallback.
    if (options.stop_token.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    std::vector<int> idxs;
    bool ok = false;
    if (auto hit = tryPllTable(cur, md, stageLimit); hit.has_value()) {
      idxs = *hit;
      ok = true;
    }
    SolveError splitErr = SolveError::Timeout;
    if (!ok) {
      // Fallback: two-phase split (corners then edges), then single-phase.
      {
        auto r1 = runIda(cur, StageKind::PllCorners, 0, a15, stageLimit,
                         hasDeadline, deadline, options.stop_token);
        if (!r1) {
          splitErr = r1.error();
        } else {
          CubeState mid = cur;
          for (int mv : *r1) mid = fastApply(mid, mv, md);
          const int used = static_cast<int>(r1->size());
          const int remaining = stageLimit - used;
          if (remaining < 0) {
            splitErr = SolveError::Timeout;
          } else {
            auto r2 = runIda(mid, StageKind::Pll, 0, a15, remaining,
                             hasDeadline, deadline, options.stop_token);
            if (!r2) {
              splitErr = r2.error();
            } else {
              idxs = *r1;
              for (int mv : *r2) idxs.push_back(mv);
              ok = true;
            }
          }
        }
      }
    }
    if (!ok) {
      if (splitErr == SolveError::Cancelled) {
        return std::unexpected(SolveError::Cancelled);
      }
      const bool timeOut =
          hasDeadline && std::chrono::steady_clock::now() >= deadline;
      if (timeOut || options.stop_token.stop_requested()) {
        return std::unexpected(SolveError::Timeout);
      }
      auto r = runIda(cur, StageKind::Pll, 0, a15, stageLimit, hasDeadline,
                      deadline, options.stop_token);
      if (!r) return std::unexpected(r.error());
      idxs = *r;
      ok = true;
    }
    const std::vector<std::string> toks = tokensFor(idxs, md);
    CubeState next = cur;
    for (int mv : idxs) next = fastApply(next, mv, md);
    if (!movesAllowed(toks, allowed_moves_for_stage("pll")) ||
        !is_pll_solved(next)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    cur = next;
    sol.stages.push_back(StageSolution{"pll", toks});
    logStage("pll", toks);
  }

  // Final verification with the frozen core (never report a bad solve).
  if (!is_solved(cur)) {
    return std::unexpected(SolveError::Unsolvable);
  }
  std::vector<Move> flat;
  for (const auto& st : sol.stages) {
    for (const std::string& t : st.moves) flat.push_back(Move{t});
  }
  if (!is_solved(apply(state, flat))) {
    return std::unexpected(SolveError::Unsolvable);
  }
  return sol;
}

}  // namespace rubik::cfop
