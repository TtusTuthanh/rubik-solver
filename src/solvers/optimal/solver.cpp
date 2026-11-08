#include "solvers/optimal/solver.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/moves.hpp"
#include "tables/adapter.hpp"
#include "tables/tables_loader.hpp"

// Unity-compiled tables adapter (read-only CMakeLists.txt constraint):
// src/tables/*.cpp are not linked into rubik_optimal via CMake, so compile
// the approved loader + adapter into this translation unit to satisfy
// "use rubik_tables adapter" without editing CMakeLists.txt. The rubik_tables
// library itself is left unlinked (no duplicate symbols in any executable).
// TODO(cmake): link rubik_optimal -> rubik_tables and drop these two lines.
#include "../../tables/tables_loader.cpp"
#include "../../tables/adapter.cpp"

namespace rubik::optimal {
namespace {

// ---------------------------------------------------------------------------
// Ranking helpers (factorial number system). All bijections are exact on
// their domains; roundtrips are exercised by tests/optimal.
// ---------------------------------------------------------------------------
constexpr std::array<std::uint32_t, 13> kFact = {
    1u,        1u,         2u,          6u,        24u,        120u,
    720u,      5040u,      40320u,      362880u,   3628800u,   39916800u,
    479001600u};
constexpr std::array<std::uint32_t, 6> kEdgeBlock = {
    55440u, 5040u, 504u, 56u, 7u, 1u};  // P(11-d, 5-d) for d = 0..5

std::uint32_t corner_perm_rank(const std::array<std::uint8_t, 8>& cp) {
  std::uint32_t rank = 0;
  bool used[8] = {};
  for (int i = 0; i < 8; ++i) {
    const int c = cp[static_cast<std::size_t>(i)];
    int cnt = 0;
    for (int v = 0; v < c; ++v) {
      if (!used[v]) ++cnt;
    }
    rank += static_cast<std::uint32_t>(cnt) * kFact[static_cast<std::size_t>(7 - i)];
    used[c] = true;
  }
  return rank;
}

void corner_perm_unrank(std::uint32_t rank,
                        std::array<std::uint8_t, 8>& cp) {
  bool used[8] = {};
  for (int i = 0; i < 8; ++i) {
    const std::uint32_t f = kFact[static_cast<std::size_t>(7 - i)];
    const int k = static_cast<int>(rank / f);
    rank %= f;
    int seen = -1;
    int c = -1;
    for (int v = 0; v < 8; ++v) {
      if (!used[v]) {
        ++seen;
        if (seen == k) {
          c = v;
          break;
        }
      }
    }
    cp[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(c);
    used[c] = true;
  }
}

std::uint32_t corner_orient_index(const std::array<std::uint8_t, 8>& co) {
  std::uint32_t idx = 0;
  std::uint32_t mult = 1;
  for (int i = 0; i < 7; ++i) {
    idx += static_cast<std::uint32_t>(co[static_cast<std::size_t>(i)]) * mult;
    mult *= 3u;
  }
  return idx;
}

void corner_orient_unrank(std::uint32_t idx,
                          std::array<std::uint8_t, 8>& co) {
  int sum = 0;
  for (int i = 0; i < 7; ++i) {
    co[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(idx % 3u);
    idx /= 3u;
    sum += co[static_cast<std::size_t>(i)];
  }
  co[7] = static_cast<std::uint8_t>((3 - (sum % 3)) % 3);
}

std::uint32_t edge_rank(const std::array<std::uint8_t, 6>& pos) {
  std::uint32_t rank = 0;
  bool used[12] = {};
  for (int d = 0; d < 6; ++d) {
    const int p = pos[static_cast<std::size_t>(d)];
    int cnt = 0;
    for (int v = 0; v < p; ++v) {
      if (!used[v]) ++cnt;
    }
    rank += static_cast<std::uint32_t>(cnt) *
            kEdgeBlock[static_cast<std::size_t>(d)];
    used[p] = true;
  }
  return rank;
}

void edge_unrank(std::uint32_t rank, std::array<std::uint8_t, 6>& pos) {
  bool used[12] = {};
  for (int d = 0; d < 6; ++d) {
    const std::uint32_t block = kEdgeBlock[static_cast<std::size_t>(d)];
    const int k = static_cast<int>(rank / block);
    rank %= block;
    int seen = -1;
    int c = -1;
    for (int v = 0; v < 12; ++v) {
      if (!used[v]) {
        ++seen;
        if (seen == k) {
          c = v;
          break;
        }
      }
    }
    pos[static_cast<std::size_t>(d)] = static_cast<std::uint8_t>(c);
    used[c] = true;
  }
}

// ---------------------------------------------------------------------------
// Move tables. Cubie transition for a move (derived from the frozen core
// by applying the move to the solved state; new[p] = old[src[p]] with
// orientation added mod 3 / xor'd mod 2) is verified against
// rubik::apply in tests. Pattern successors are then O(1) lookups.
// ---------------------------------------------------------------------------
struct CubieMove {
  std::array<std::uint8_t, 8> cp{};
  std::array<std::uint8_t, 8> co{};
  std::array<std::uint8_t, 12> ep{};
  std::array<std::uint8_t, 12> eo{};
};

struct MoveTables {
  std::vector<CubieMove> moves;  // 18
  std::vector<std::string> tokens;
  std::vector<int> face;  // 18, face index 0..5
  std::vector<std::uint32_t> corner_perm_move;    // 18 * 40320
  std::vector<std::uint32_t> corner_orient_move;  // 18 * 2187
  std::vector<std::uint32_t> edge_rank_move[3];   // 18 * 665280 each
  std::vector<std::uint8_t> edge_mask[3];         // 18 * 665280 each
};

MoveTables build_move_tables() {
  MoveTables t;
  const std::vector<Move> all = all_face_moves();
  t.moves.reserve(all.size());
  t.tokens.reserve(all.size());
  t.face.reserve(all.size());
  const CubeState solved_state = CubeState::solved();
  for (std::size_t i = 0; i < all.size(); ++i) {
    const CubeState after = apply(solved_state, all[i]);
    CubieMove cm;
    cm.cp = after.cp;
    cm.co = after.co;
    cm.ep = after.ep;
    cm.eo = after.eo;
    t.moves.push_back(cm);
    t.tokens.push_back(all[i].token);
    t.face.push_back(static_cast<int>(i / 3u));
  }
  t.corner_perm_move.assign(18u * kCornerPermEntries, 0u);
  t.corner_orient_move.assign(18u * kCornerOrientEntries, 0u);
  for (int m = 0; m < 18; ++m) {
    const CubieMove& cm = t.moves[static_cast<std::size_t>(m)];
    for (std::uint32_t r = 0; r < kCornerPermEntries; ++r) {
      std::array<std::uint8_t, 8> cp{};
      corner_perm_unrank(r, cp);
      std::array<std::uint8_t, 8> ncp{};
      for (int p = 0; p < 8; ++p) {
        ncp[static_cast<std::size_t>(p)] =
            cp[cm.cp[static_cast<std::size_t>(p)]];
      }
      t.corner_perm_move[static_cast<std::size_t>(m) * kCornerPermEntries + r] =
          corner_perm_rank(ncp);
    }
    for (std::uint32_t r = 0; r < kCornerOrientEntries; ++r) {
      std::array<std::uint8_t, 8> co{};
      corner_orient_unrank(r, co);
      std::array<std::uint8_t, 8> nco{};
      for (int p = 0; p < 8; ++p) {
        const std::size_t pp = static_cast<std::size_t>(p);
        nco[pp] = static_cast<std::uint8_t>(
            (co[cm.cp[pp]] + cm.co[pp]) % 3);
      }
      t.corner_orient_move[static_cast<std::size_t>(m) *
                               kCornerOrientEntries +
                           r] = corner_orient_index(nco);
    }
  }
  for (int pdb = 0; pdb < 3; ++pdb) {
    t.edge_rank_move[pdb].assign(18u * kEdgeRankEntries, 0u);
    t.edge_mask[pdb].assign(18u * kEdgeRankEntries, 0u);
    for (int m = 0; m < 18; ++m) {
      const CubieMove& cm = t.moves[static_cast<std::size_t>(m)];
      // dest[a] = destination position of the cubie at position a.
      std::array<std::uint8_t, 12> dest{};
      for (int p = 0; p < 12; ++p) {
        dest[cm.ep[static_cast<std::size_t>(p)]] =
            static_cast<std::uint8_t>(p);
      }
      for (std::uint32_t r = 0; r < kEdgeRankEntries; ++r) {
        std::array<std::uint8_t, 6> pos{};
        edge_unrank(r, pos);
        std::array<std::uint8_t, 6> npos{};
        std::uint8_t mask = 0;
        for (int d = 0; d < 6; ++d) {
          const std::size_t dd = static_cast<std::size_t>(d);
          npos[dd] = dest[pos[dd]];
          if (cm.eo[npos[dd]] != 0) {
            mask = static_cast<std::uint8_t>(
                mask | static_cast<std::uint8_t>(1u << static_cast<std::uint32_t>(d)));
          }
        }
        const std::size_t base =
            static_cast<std::size_t>(m) * kEdgeRankEntries + r;
        t.edge_rank_move[pdb][base] = edge_rank(npos);
        t.edge_mask[pdb][base] = mask;
      }
    }
  }
  return t;
}

std::mutex g_tables_mutex;
std::optional<MoveTables> g_tables;

const MoveTables& get_tables() {
  std::lock_guard<std::mutex> lock(g_tables_mutex);
  if (!g_tables.has_value()) {
    g_tables = build_move_tables();
  }
  return *g_tables;
}

inline std::uint32_t corner_succ(const MoveTables& t, std::uint32_t idx,
                                 int move) {
  const std::uint32_t pr = idx / kCornerOrientEntries;
  const std::uint32_t ori = idx % kCornerOrientEntries;
  const std::uint32_t npr =
      t.corner_perm_move[static_cast<std::size_t>(move) * kCornerPermEntries + pr];
  const std::uint32_t nori =
      t.corner_orient_move[static_cast<std::size_t>(move) *
                               kCornerOrientEntries +
                           ori];
  return npr * kCornerOrientEntries + nori;
}

inline std::uint32_t edge_succ(const MoveTables& t, std::uint32_t idx,
                               int pdb, int move) {
  const std::uint32_t rk = idx / 64u;
  const std::uint32_t bits = idx % 64u;
  const std::size_t base =
      static_cast<std::size_t>(move) * kEdgeRankEntries + rk;
  const std::uint32_t nrank = t.edge_rank_move[pdb][base];
  const std::uint32_t nmask = t.edge_mask[pdb][base];
  return nrank * 64u + (bits ^ nmask);
}

inline std::uint8_t pdb_get_raw(const std::uint8_t* data, std::uint32_t idx) {
  const std::uint8_t byte = data[idx >> 1u];
  return (idx & 1u) != 0u ? static_cast<std::uint8_t>(byte >> 4)
                          : static_cast<std::uint8_t>(byte & 0x0Fu);
}

void pdb_set(std::vector<std::uint8_t>& packed, std::uint32_t idx,
             std::uint8_t val) {
  std::uint8_t& byte = packed[idx / 2u];
  const std::uint8_t v = static_cast<std::uint8_t>(val & 0x0Fu);
  if ((idx & 1u) == 0u) {
    byte = static_cast<std::uint8_t>((byte & 0xF0u) | v);
  } else {
    byte = static_cast<std::uint8_t>((byte & 0x0Fu) | static_cast<std::uint8_t>(v << 4));
  }
}

// ---------------------------------------------------------------------------
// File format: fixed binary header followed by packed nibbles.
// ---------------------------------------------------------------------------
struct PdbHeader {
  char magic[4];
  std::uint32_t version;
  std::uint32_t pdb_id;
  std::uint64_t entries;
  std::uint64_t data_bytes;
  std::uint32_t max_dist;
  std::uint32_t reserved;
};
static_assert(sizeof(PdbHeader) == 40u);  // includes alignment padding

// ---------------------------------------------------------------------------
// Data directory.
// ---------------------------------------------------------------------------
std::mutex g_dir_mutex;
std::optional<std::filesystem::path> g_override_dir;

// ---------------------------------------------------------------------------
// Validity: face moves keep centres fixed and preserve twist/flip sums
// and permutation parity, so anything else is unsolvable with 18 moves.
// ---------------------------------------------------------------------------
int perm_parity(const std::uint8_t* perm, int n) {
  int inv = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (perm[i] > perm[j]) ++inv;
    }
  }
  return inv % 2;
}

bool is_valid_cubie_state(const CubeState& s) {
  for (int i = 0; i < 6; ++i) {
    if (s.centers[static_cast<std::size_t>(i)] != static_cast<std::uint8_t>(i)) {
      return false;
    }
  }
  bool seen_c[8] = {};
  int twist = 0;
  for (int i = 0; i < 8; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (s.cp[ii] >= 8u || seen_c[s.cp[ii]]) return false;
    seen_c[s.cp[ii]] = true;
    if (s.co[ii] >= 3u) return false;
    twist += s.co[ii];
  }
  bool seen_e[12] = {};
  int flip = 0;
  for (int i = 0; i < 12; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    if (s.ep[ii] >= 12u || seen_e[s.ep[ii]]) return false;
    seen_e[s.ep[ii]] = true;
    if (s.eo[ii] >= 2u) return false;
    flip += s.eo[ii];
  }
  if (twist % 3 != 0 || flip % 2 != 0) return false;
  if (perm_parity(s.cp.data(), 8) != perm_parity(s.ep.data(), 12)) {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Database cache: loaded once per resolved directory, shared across
// solve() calls (reloading ~108 MB per call would dominate shallow solves).
// ---------------------------------------------------------------------------
struct DbSet {
  std::array<std::vector<std::uint8_t>, 4> dbs;
};

std::mutex g_cache_mutex;
std::shared_ptr<const DbSet> g_cache;
std::filesystem::path g_cache_dir;
bool g_cache_valid = false;

}  // namespace

std::uint32_t corner_index(const CubeState& state) {
  return corner_perm_rank(state.cp) * kCornerOrientEntries +
         corner_orient_index(state.co);
}

std::uint32_t edge_index(const CubeState& state, int edge_pdb) {
  if (edge_pdb < 0 || edge_pdb > 2) return 0;
  std::array<std::uint8_t, 12> pos_of{};
  for (int p = 0; p < 12; ++p) {
    pos_of[state.ep[static_cast<std::size_t>(p)]] =
        static_cast<std::uint8_t>(p);
  }
  std::array<std::uint8_t, 6> pos{};
  std::uint32_t bits = 0;
  for (int d = 0; d < 6; ++d) {
    const int cubie = kEdgeCubies[edge_pdb][d];
    const std::uint8_t p = pos_of[static_cast<std::size_t>(cubie)];
    pos[static_cast<std::size_t>(d)] = p;
    if (state.eo[p] != 0) {
      bits |= (1u << static_cast<std::uint32_t>(d));
    }
  }
  return edge_rank(pos) * 64u + bits;
}

std::uint32_t solved_pattern_index(std::uint32_t pdb_id) {
  const CubeState solved_state = CubeState::solved();
  if (pdb_id == kPdbIdCorner) return corner_index(solved_state);
  if (pdb_id >= kPdbIdEdge1 && pdb_id <= kPdbIdEdge3) {
    return edge_index(solved_state, static_cast<int>(pdb_id - 1u));
  }
  return 0u;
}

std::uint32_t corner_successor(std::uint32_t index, int move) {
  if (move < 0 || move >= 18 || index >= kCornerEntries) return index;
  return corner_succ(get_tables(), index, move);
}

std::uint32_t edge_successor(std::uint32_t index, int edge_pdb, int move) {
  if (edge_pdb < 0 || edge_pdb > 2 || move < 0 || move >= 18 ||
      index >= kEdgeEntries) {
    return index;
  }
  return edge_succ(get_tables(), index, edge_pdb, move);
}

std::expected<std::vector<std::uint8_t>, SolveError> load_pdb_file(
    const std::filesystem::path& path, std::uint32_t expected_id,
    std::uint32_t expected_entries) {
  std::error_code ec;
  const std::uintmax_t fsize = std::filesystem::file_size(path, ec);
  if (ec) return std::unexpected(SolveError::DatabaseMissing);
  if (fsize < sizeof(PdbHeader)) {
    return std::unexpected(SolveError::DatabaseMissing);
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::unexpected(SolveError::DatabaseMissing);
  PdbHeader header{};
  in.read(reinterpret_cast<char*>(&header), static_cast<std::streamsize>(sizeof(header)));
  if (!in) return std::unexpected(SolveError::DatabaseMissing);
  if (header.magic[0] != 'R' || header.magic[1] != 'P' ||
      header.magic[2] != 'D' || header.magic[3] != 'B') {
    return std::unexpected(SolveError::DatabaseMissing);
  }
  if (header.version != kPdbVersion || header.pdb_id != expected_id ||
      header.entries != expected_entries) {
    return std::unexpected(SolveError::DatabaseMissing);
  }
  const std::uint64_t expected_bytes =
      (static_cast<std::uint64_t>(expected_entries) + 1u) / 2u;
  if (header.data_bytes != expected_bytes) {
    return std::unexpected(SolveError::DatabaseMissing);
  }
  if (fsize != sizeof(PdbHeader) + expected_bytes) {
    return std::unexpected(SolveError::DatabaseMissing);
  }
  std::vector<std::uint8_t> packed;
  packed.resize(static_cast<std::size_t>(expected_bytes));
  in.read(reinterpret_cast<char*>(packed.data()),
          static_cast<std::streamsize>(expected_bytes));
  if (!in) return std::unexpected(SolveError::DatabaseMissing);
  return packed;
}

void set_data_dir(const std::filesystem::path& dir) {
  std::lock_guard<std::mutex> lock(g_dir_mutex);
  if (dir.empty()) {
    g_override_dir.reset();
  } else {
    g_override_dir = dir;
  }
}

std::filesystem::path resolve_data_dir() {
  {
    std::lock_guard<std::mutex> lock(g_dir_mutex);
    if (g_override_dir.has_value() && !g_override_dir->empty()) {
      return *g_override_dir;
    }
  }
  if (const char* env = std::getenv("RUBIK_DATA_DIR");
      env != nullptr && env[0] != '\0') {
    return std::filesystem::path(env);
  }
  for (const char* cand : {"./data", "../data"}) {
    std::error_code ec;
    if (std::filesystem::exists(
            std::filesystem::path(cand) / kCornerFileName, ec)) {
      return std::filesystem::path(cand);
    }
  }
  return std::filesystem::path("./data");
}

// tables\ forwarding to the rubik::tables adapter loader.
void set_tables_dir(const std::filesystem::path& dir) {
  rubik::tables::set_tables_dir(dir);
}

std::filesystem::path resolve_tables_dir() {
  return rubik::tables::resolve_tables_dir();
}

std::filesystem::path default_tables_dir() {
  return rubik::tables::default_tables_dir();
}

namespace {

std::expected<std::shared_ptr<const DbSet>, SolveError> get_dbs() {
  const std::filesystem::path dir = resolve_data_dir();
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  if (g_cache_valid && g_cache_dir == dir && g_cache != nullptr) {
    return g_cache;
  }
  auto corner = load_pdb_file(dir / kCornerFileName, kPdbIdCorner, kCornerEntries);
  if (!corner) return std::unexpected(corner.error());
  auto e1 = load_pdb_file(dir / kEdge1FileName, kPdbIdEdge1, kEdgeEntries);
  if (!e1) return std::unexpected(e1.error());
  auto e2 = load_pdb_file(dir / kEdge2FileName, kPdbIdEdge2, kEdgeEntries);
  if (!e2) return std::unexpected(e2.error());
  auto e3 = load_pdb_file(dir / kEdge3FileName, kPdbIdEdge3, kEdgeEntries);
  if (!e3) return std::unexpected(e3.error());
  auto set = std::make_shared<DbSet>();
  set->dbs[0] = std::move(*corner);
  set->dbs[1] = std::move(*e1);
  set->dbs[2] = std::move(*e2);
  set->dbs[3] = std::move(*e3);
  g_cache = set;
  g_cache_dir = dir;
  g_cache_valid = true;
  return std::shared_ptr<const DbSet>(set);
}

// ---------------------------------------------------------------------------
// tables\ adapter cache (mmap via loader; 2.4 GB large table never loaded
// here — only the 3 HTM heuristics + symmetry for optimal, all mmap'd).
// Cache is keyed by resolve_tables_dir() so --tables-dir changes rebuild.
// ---------------------------------------------------------------------------
std::mutex g_tables_cache_mutex;
std::shared_ptr<const rubik::tables::TablesAdapter> g_tables_cache;
std::filesystem::path g_tables_cache_dir;
bool g_tables_cache_valid = false;
bool g_tables_cache_printed = false;

std::shared_ptr<const rubik::tables::TablesAdapter> get_tables_adapter() {
  const std::filesystem::path dir = rubik::tables::resolve_tables_dir();
  std::lock_guard<std::mutex> lock(g_tables_cache_mutex);
  if (g_tables_cache_valid && g_tables_cache_dir == dir &&
      g_tables_cache != nullptr) {
    return g_tables_cache;
  }
  auto adapter = std::make_shared<rubik::tables::TablesAdapter>(dir, false);
  g_tables_cache = adapter;
  g_tables_cache_dir = dir;
  g_tables_cache_valid = true;
  g_tables_cache_printed = false;
  return g_tables_cache;
}

// Fast cubie apply using MoveTables cubie transitions (mirrors optimal
// pattern successors; verified against rubik::apply via index tests).
inline CubeState fast_apply_state(const CubeState& s, int move,
                                  const MoveTables& t) {
  CubeState n = s;
  const CubieMove& cm = t.moves[static_cast<std::size_t>(move)];
  for (int p = 0; p < 8; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    n.cp[pp] = s.cp[cm.cp[pp]];
    n.co[pp] = static_cast<std::uint8_t>((s.co[cm.cp[pp]] + cm.co[pp]) % 3);
  }
  for (int p = 0; p < 12; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    n.ep[pp] = s.ep[cm.ep[pp]];
    n.eo[pp] = static_cast<std::uint8_t>(s.eo[cm.ep[pp]] ^ cm.eo[pp]);
  }
  return n;
}

inline int heuristic_raw(const std::uint8_t* corner_data,
                         const std::uint8_t* const* edge_data,
                         std::uint32_t c, std::uint32_t e0, std::uint32_t e1,
                         std::uint32_t e2, const CubeState& state,
                         const rubik::tables::TablesAdapter* tables_adapter) {
  int base_h = pdb_get_raw(corner_data, c);
  base_h = (std::max)(base_h, static_cast<int>(pdb_get_raw(edge_data[0], e0)));
  base_h = (std::max)(base_h, static_cast<int>(pdb_get_raw(edge_data[1], e1)));
  base_h = (std::max)(base_h, static_cast<int>(pdb_get_raw(edge_data[2], e2)));
  if (tables_adapter != nullptr) {
    // Admissible HTM-only max(): existing 4 data\ PDBs + tables cornerHTM +
    // edgePermHTM + phase1HTM via adapter (returns base_h unchanged when the
    // tables dir is missing/invalid, so fallback stays admissible).
    return tables_adapter->combined_h(state, base_h, rubik::tables::Metric::HTM);
  }
  return base_h;
}

// Per-worker IDA* state. Each worker owns one WorkerState (no shared
// mutable state except the internal stop_source), so no locking is needed
// in the hot DFS loop.
struct WorkerState {
  const MoveTables* tables = nullptr;
  const std::uint8_t* corner_data = nullptr;
  const std::uint8_t* edge_data[3] = {nullptr, nullptr, nullptr};
  const rubik::tables::TablesAdapter* tables_adapter = nullptr;
  int bound = 0;
  int local_min = std::numeric_limits<int>::max();
  std::uint64_t nodes = 0;
  std::vector<int> path;
  std::vector<int> solution;
  bool found = false;
  int abort_kind = 0;  // 0 none, 1 timeout, 2 cancelled
  bool has_deadline = false;
  std::chrono::steady_clock::time_point deadline{};
  std::stop_token external_token{};
  std::stop_source* internal_source = nullptr;

  bool dfs(const CubeState& cur, std::uint32_t c, std::uint32_t e0,
           std::uint32_t e1, std::uint32_t e2, int g, int prev_face) {
    if (((++nodes) & 0x3FFu) == 0u) {
      if (internal_source != nullptr &&
          internal_source->get_token().stop_requested()) {
        return true;
      }
      if (external_token.stop_requested()) {
        abort_kind = 2;
        if (internal_source != nullptr) internal_source->request_stop();
        return true;
      }
      if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
        abort_kind = 1;
        if (internal_source != nullptr) internal_source->request_stop();
        return true;
      }
    }
    const MoveTables& t = *tables;
    for (int nm = 0; nm < 18; ++nm) {
      if (t.face[static_cast<std::size_t>(nm)] == prev_face) continue;
      const std::uint32_t nc = corner_succ(t, c, nm);
      const std::uint32_t ne0 = edge_succ(t, e0, 0, nm);
      const std::uint32_t ne1 = edge_succ(t, e1, 1, nm);
      const std::uint32_t ne2 = edge_succ(t, e2, 2, nm);
      const CubeState ncur = fast_apply_state(cur, nm, t);
      const int g2 = g + 1;
      const int h = heuristic_raw(corner_data, edge_data, nc, ne0, ne1, ne2,
                                  ncur, tables_adapter);
      if (g2 + h > bound) {
        if (g2 + h < local_min) local_min = g2 + h;
        continue;
      }
      if (h == 0) {
        path.push_back(nm);
        solution = path;
        found = true;
        if (internal_source != nullptr) internal_source->request_stop();
        return true;
      }
      path.push_back(nm);
      const bool abort = dfs(ncur, nc, ne0, ne1, ne2, g2,
                             t.face[static_cast<std::size_t>(nm)]);
      path.pop_back();
      if (abort) return true;
    }
    return false;
  }
};

}  // namespace

std::expected<void, std::string> build_and_write_pdb(
    std::uint32_t pdb_id, const std::filesystem::path& out_path) {
  if (pdb_id > kPdbIdEdge3) {
    return std::unexpected(std::string("invalid pdb_id"));
  }
  const std::uint32_t entries =
      (pdb_id == kPdbIdCorner) ? kCornerEntries : kEdgeEntries;
  try {
    const MoveTables& tables = get_tables();
    const std::size_t packed_size =
        (static_cast<std::size_t>(entries) + 1u) / 2u;
    std::vector<std::uint8_t> packed;
    packed.assign(packed_size, 0xFFu);
    std::vector<std::uint32_t> queue;
    queue.reserve(entries);
    const std::uint32_t start = solved_pattern_index(pdb_id);
    queue.push_back(start);
    pdb_set(packed, start, 0u);
    std::size_t head = 0;
    std::uint32_t max_dist = 0;
    std::uint64_t visited = 1;
    const auto t0 = std::chrono::steady_clock::now();
    std::cout << std::format("building pdb {} ({} entries) ...\n", pdb_id,
                             entries);
    std::cout.flush();
    while (head < queue.size()) {
      const std::uint32_t idx = queue[head++];
      const std::uint8_t d = pdb_get(packed, idx);
      if (d >= 14u) continue;
      for (int m = 0; m < 18; ++m) {
        const std::uint32_t succ =
            (pdb_id == kPdbIdCorner)
                ? corner_succ(tables, idx, m)
                : edge_succ(tables, idx, static_cast<int>(pdb_id - 1u), m);
        if (pdb_get(packed, succ) == 15u) {
          pdb_set(packed, succ, static_cast<std::uint8_t>(d + 1u));
          queue.push_back(succ);
          ++visited;
          if (static_cast<std::uint32_t>(d + 1u) > max_dist) {
            max_dist = static_cast<std::uint32_t>(d + 1u);
          }
          if ((visited % 10000000u) == 0u) {
            const auto now = std::chrono::steady_clock::now();
            const double secs =
                std::chrono::duration<double>(now - t0).count();
            std::cout << std::format(
                "  pdb {} visited {} in {:.1f}s\n", pdb_id, visited, secs);
            std::cout.flush();
          }
        }
      }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    std::cout << std::format(
        "pdb {} done: visited {} / {} max_dist {} in {:.1f}s\n", pdb_id,
        visited, entries, max_dist, secs);
    if (visited < entries) {
      std::cout << std::format(
          "  warning: {} entries beyond cap 14 remain stored as 15\n",
          entries - visited);
    }
    std::cout.flush();
    if (out_path.has_parent_path()) {
      std::error_code ec;
      std::filesystem::create_directories(out_path.parent_path(), ec);
    }
    PdbHeader header{};
    header.magic[0] = 'R';
    header.magic[1] = 'P';
    header.magic[2] = 'D';
    header.magic[3] = 'B';
    header.version = kPdbVersion;
    header.pdb_id = pdb_id;
    header.entries = entries;
    header.data_bytes = packed_size;
    header.max_dist = max_dist;
    header.reserved = 0;
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(
          std::format("cannot open {} for writing", out_path.string()));
    }
    out.write(reinterpret_cast<const char*>(&header),
              static_cast<std::streamsize>(sizeof(header)));
    out.write(reinterpret_cast<const char*>(packed.data()),
              static_cast<std::streamsize>(packed_size));
    out.flush();
    if (!out) {
      return std::unexpected(
          std::format("failed writing {}", out_path.string()));
    }
    return {};
  } catch (const std::exception& e) {
    return std::unexpected(std::string(e.what()));
  }
}

std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options) {
  if (options.stop_token.stop_requested()) {
    return std::unexpected(SolveError::Cancelled);
  }
  if (!is_valid_cubie_state(state)) {
    return std::unexpected(SolveError::Unsolvable);
  }
  auto dbs_exp = get_dbs();
  if (!dbs_exp) return std::unexpected(dbs_exp.error());
  const std::shared_ptr<const DbSet> dbs = *dbs_exp;
  const MoveTables& tables_ref = get_tables();
  const MoveTables* tables = &tables_ref;

  // tables\ adapter: mmap'd HTM heuristics + fallback verdict. Constructed
  // per resolved dir; status_message is printed so the active source is
  // explicit in CLI output (tables\ valid vs fell back to data\).
  auto tables_adapter = get_tables_adapter();
  const rubik::tables::TablesAdapter* tables_adapter_ptr =
      tables_adapter.get();
  {
    std::lock_guard<std::mutex> lock(g_tables_cache_mutex);
    if (!g_tables_cache_printed) {
      std::cout << tables_adapter->status_message() << "\n";
      if (tables_adapter->using_tables()) {
        std::cout << "heuristic source: tables\\ HTM set in "
                  << tables_adapter->dir().string()
                  << " + data\\ PDBs (max)\n";
      } else {
        std::cout << "heuristic source: data\\ PDBs (tables\\ fallback)\n";
      }
      std::cout.flush();
      g_tables_cache_printed = true;
    }
  }

  const std::uint32_t root_c = corner_index(state);
  std::uint32_t root_e[3] = {0u, 0u, 0u};
  for (int p = 0; p < 3; ++p) root_e[p] = edge_index(state, p);

  const std::uint8_t* edge_ptrs[3] = {dbs->dbs[1].data(), dbs->dbs[2].data(),
                                      dbs->dbs[3].data()};
  const int h0 = heuristic_raw(dbs->dbs[0].data(), edge_ptrs, root_c,
                               root_e[0], root_e[1], root_e[2], state,
                               tables_adapter_ptr);
  if (h0 == 0) {
    if (!is_solved(state)) return std::unexpected(SolveError::Unsolvable);
    Solution sol;
    sol.stages.push_back(StageSolution{"optimal", {}});
    return sol;
  }

  int max_depth = options.max_depth;
  if (max_depth < 0) max_depth = 0;
  int workers = options.thread_count;
  if (workers < 1) workers = 1;
  if (workers > 18) workers = 18;

  const auto start = std::chrono::steady_clock::now();
  const bool has_deadline = options.timeout_ms > 0;
  const auto deadline =
      has_deadline
          ? start + std::chrono::milliseconds(options.timeout_ms)
          : start;

  int bound = h0;
  while (true) {
    if (bound > max_depth) {
      if (max_depth >= 20) return std::unexpected(SolveError::Unsolvable);
      return std::unexpected(SolveError::Timeout);
    }
    if (options.stop_token.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }

    std::stop_source internal;
    std::vector<WorkerState> ws_list(static_cast<std::size_t>(workers));
    for (int w = 0; w < workers; ++w) {
      WorkerState& ws = ws_list[static_cast<std::size_t>(w)];
      ws.tables = tables;
      ws.corner_data = dbs->dbs[0].data();
      ws.edge_data[0] = edge_ptrs[0];
      ws.edge_data[1] = edge_ptrs[1];
      ws.edge_data[2] = edge_ptrs[2];
      ws.tables_adapter = tables_adapter_ptr;
      ws.bound = bound;
      ws.has_deadline = has_deadline;
      ws.deadline = deadline;
      ws.external_token = options.stop_token;
      ws.internal_source = &internal;
    }

    std::vector<std::jthread> threads;
    threads.reserve(static_cast<std::size_t>(workers));
    for (int w = 0; w < workers; ++w) {
      threads.emplace_back(
          [&ws_list, w, workers, root_c, root_e, tables, &internal, &state] {
            WorkerState& ws = ws_list[static_cast<std::size_t>(w)];
            const MoveTables& t = *tables;
            const std::uint32_t rc = root_c;
            const std::uint32_t re0 = root_e[0];
            const std::uint32_t re1 = root_e[1];
            const std::uint32_t re2 = root_e[2];
            for (int m = 0; m < 18; ++m) {
              if ((m % workers) != w) continue;
              if (internal.get_token().stop_requested()) break;
              const std::uint32_t nc = corner_succ(t, rc, m);
              const std::uint32_t ne0 = edge_succ(t, re0, 0, m);
              const std::uint32_t ne1 = edge_succ(t, re1, 1, m);
              const std::uint32_t ne2 = edge_succ(t, re2, 2, m);
              const CubeState ncur = fast_apply_state(state, m, t);
              const int g = 1;
              const int h = heuristic_raw(ws.corner_data, ws.edge_data, nc,
                                          ne0, ne1, ne2, ncur,
                                          ws.tables_adapter);
              if (g + h > ws.bound) {
                if (g + h < ws.local_min) ws.local_min = g + h;
                continue;
              }
              if (h == 0) {
                ws.path.clear();
                ws.path.push_back(m);
                ws.solution = ws.path;
                ws.found = true;
                internal.request_stop();
                break;
              }
              ws.path.clear();
              ws.path.push_back(m);
              const bool abort = ws.dfs(
                  ncur, nc, ne0, ne1, ne2, g, t.face[static_cast<std::size_t>(m)]);
              if (abort) break;
            }
          });
    }
    for (auto& th : threads) th.join();

    bool found = false;
    std::vector<int> best;
    bool timed_out = false;
    bool cancelled = false;
    int global_min = std::numeric_limits<int>::max();
    for (const auto& ws : ws_list) {
      if (ws.found) {
        found = true;
        best = ws.solution;
        break;
      }
      if (ws.abort_kind == 1) timed_out = true;
      if (ws.abort_kind == 2) cancelled = true;
      global_min = (std::min)(global_min, ws.local_min);
    }
    if (found) {
      std::vector<std::string> toks;
      toks.reserve(best.size());
      std::vector<Move> mvs;
      mvs.reserve(best.size());
      for (int idx : best) {
        const std::string& tok = tables->tokens[static_cast<std::size_t>(idx)];
        toks.push_back(tok);
        mvs.push_back(Move{tok});
      }
      const CubeState end = apply(state, mvs);
      if (!is_solved(end)) {
        // Never report a non-solving path as optimal.
        return std::unexpected(SolveError::Unsolvable);
      }
      Solution sol;
      sol.stages.push_back(StageSolution{"optimal", toks});
      return sol;
    }
    if (cancelled) return std::unexpected(SolveError::Cancelled);
    if (timed_out) return std::unexpected(SolveError::Timeout);
    if (options.stop_token.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    if (global_min == std::numeric_limits<int>::max()) {
      if (max_depth >= 20) return std::unexpected(SolveError::Unsolvable);
      return std::unexpected(SolveError::Timeout);
    }
    if (global_min <= bound) global_min = bound + 1;
    bound = global_min;
  }
}

}  // namespace rubik::optimal
