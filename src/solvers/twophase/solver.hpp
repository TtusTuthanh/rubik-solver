#pragma once
// Two-phase (Kociemba) solver: G1 (phase1) -> solved (phase2).
// Header-inline so it builds without editing read-only CMakeLists.txt;
// src/solvers/twophase/solver.cpp just includes this header.
// Stages: phase1 (any 18 HTM face moves, goal is_in_g1), phase2
// (10 G1-preserving moves U/U'/U2/D/D'/D2/R2/L2/F2/B2, goal is_solved).
// Heuristics use tables\ symmetry + phase2 accessors + phase1 movement
// accessors (mmap'd, read-only). Falls back to h=0 when tables\ is
// missing/invalid (still functional for shallow solves) and prints
// the adapter status_message so the active source is explicit.
// Frozen solve(CubeState, SolveOptions) signature is unchanged.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"
#include "core/moves.hpp"
#include "tables/adapter.hpp"
#include "tables/tables_loader.hpp"

namespace rubik::twophase {

// G1 predicate: all corner/edge orientations solved and the equatorial
// slice positions 8..11 hold the slice cubies 8..11 (FR,FL,BL,BR).
inline bool is_in_g1(const CubeState& s) {
  for (const auto v : s.co) {
    if (v != 0) return false;
  }
  for (const auto v : s.eo) {
    if (v != 0) return false;
  }
  bool seen[12] = {};
  for (int p = 8; p < 12; ++p) {
    const auto cubie = s.ep[static_cast<std::size_t>(p)];
    if (cubie < 8 || cubie >= 12) return false;
    if (seen[cubie]) return false;
    seen[cubie] = true;
  }
  return true;
}

inline bool is_phase1_solved(const CubeState& s) { return is_in_g1(s); }
inline bool is_phase2_solved(const CubeState& s) { return is_solved(s); }

namespace detail {

inline int perm_parity(const std::uint8_t* perm, int n) {
  int inv = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (perm[i] > perm[j]) ++inv;
    }
  }
  return inv % 2;
}

inline bool is_valid_cubie_state(const CubeState& s) {
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
  if (perm_parity(s.cp.data(), 8) != perm_parity(s.ep.data(), 12)) return false;
  return true;
}

struct CubieMove {
  std::array<std::uint8_t, 8> cp{};
  std::array<std::uint8_t, 8> co{};
  std::array<std::uint8_t, 12> ep{};
  std::array<std::uint8_t, 12> eo{};
};

struct MoveData {
  std::array<CubieMove, 18> moves{};
  std::array<std::string, 18> tokens{};
  std::array<int, 18> face{};
};

inline MoveData build_move_data() {
  MoveData t;
  const std::vector<Move> all = all_face_moves();
  const CubeState solved_state = CubeState::solved();
  for (std::size_t i = 0; i < all.size() && i < 18; ++i) {
    const CubeState after = apply(solved_state, all[i]);
    t.moves[i].cp = after.cp;
    t.moves[i].co = after.co;
    t.moves[i].ep = after.ep;
    t.moves[i].eo = after.eo;
    t.tokens[i] = all[i].token;
    t.face[i] = static_cast<int>(i / 3u);
  }
  return t;
}

inline const MoveData& get_move_data() {
  static const MoveData data = build_move_data();
  return data;
}

inline CubeState fast_apply(const CubeState& s, int move, const MoveData& t) {
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

// tables adapter cache (keyed by resolved dir; mmap'd, read-only).
inline std::shared_ptr<rubik::tables::TablesAdapter> get_adapter() {
  static std::mutex m;
  static std::shared_ptr<rubik::tables::TablesAdapter> cache;
  static std::filesystem::path cache_dir;
  static bool valid = false;
  const std::filesystem::path dir = rubik::tables::resolve_tables_dir();
  std::lock_guard<std::mutex> lock(m);
  if (valid && cache_dir == dir && cache != nullptr) return cache;
  cache = std::make_shared<rubik::tables::TablesAdapter>(dir, false);
  cache_dir = dir;
  valid = true;
  std::cout << cache->status_message() << "\n";
  if (cache->using_tables()) {
    std::cout << "twophase heuristic source: tables HTM set in "
              << cache->dir().string() << "\n";
  } else {
    std::cout << "twophase heuristic source: fallback (tables missing; h=0 stub)\n";
  }
  std::cout.flush();
  return cache;
}

// Phase-1 heuristic: distance to G1 (tables). Admissible for phase1 goal.
inline int heuristic_phase1(const CubeState& s,
                            const rubik::tables::TablesAdapter& adapter) {
  return adapter.h_phase1(s, rubik::tables::Metric::HTM);
}

// Phase-2 heuristic via symmetry + parity-halved 2-bit table.
// Returns 0 when tables are unavailable (admissible stub).
// Tables are preloaded once (lazily on first call); each call is a
// pure lookup with no load attempts or string construction.
inline int heuristic_phase2(const CubeState& s,
                            rubik::tables::TablesAdapter& adapter) {
  static const int kSymIdx =
      rubik::tables::TableLoader::index_of("kociembaPhase2Symmetry");
  static bool sym_attempted = false;
  if (!adapter.using_tables()) return 0;
  auto& loader = adapter.loader();
  if (!sym_attempted) {
    (void)loader.load_indexed(static_cast<std::size_t>(kSymIdx), false);
    sym_attempted = true;
  }
  if (loader.view_indexed(static_cast<std::size_t>(kSymIdx)) == nullptr) return 0;
  // Magnitudes: the 2-bit phase-2 table stores distance mod 3 only
  // (max value 2), so it cannot guide deep phase-2 search alone.
  // The corner/edge pattern databases measure full distance to
  // solved, admissible for the G1-restricted phase-2 goal
  // (fewer allowed moves can only lengthen the shortest path), and
  // are essentially exact at shallow depths. Take the max.
  rubik::tables::Phase2Coords c;
  rubik::tables::upstream_phase2_coords(s, c);
  const std::uint32_t sym = loader.phase2_min_orientation(c.corner_perm);
  const std::uint32_t sym_cp = loader.phase2_corner_sym(c.corner_perm, sym);
  // Symmetric position: the same orientation symmetrises corner,
  // UD and slice coordinates before indexing (upstream
  // getPhase2SymmetricPosition). Passing raw UD/slice indexes a
  // wrong table cell (unrelated entry) and destroys admissibility.
  const std::uint32_t sym_ud = loader.phase2_ud_sym(c.ud_perm, sym);
  const std::uint32_t sym_sl = loader.phase2_e_sym(c.slice_perm, sym);
  const std::uint32_t cls = loader.phase2_equiv_class(sym_cp);
  const int h2 = adapter.h_phase2_from_coords(cls, sym_ud, sym_sl,
                                              rubik::tables::Metric::HTM);
  const int hc = adapter.h_corner(s, rubik::tables::Metric::HTM);
  const int he = adapter.h_edge_perm(s, rubik::tables::Metric::HTM);
  return std::max(h2, std::max(hc, he));
}

struct Searcher {
  const MoveData* mv = nullptr;
  const std::vector<int>* allowed = nullptr;
  bool phase1_goal = false;
  rubik::tables::TablesAdapter* adapter = nullptr;
  int best_exceed = 0;
  std::vector<int> path;
  std::vector<int> solution;
  bool found = false;
  int abort_kind = 0;  // 0 none, 1 timeout, 2 cancelled
  std::uint64_t nodes = 0;
  bool has_deadline = false;
  std::chrono::steady_clock::time_point deadline{};
  std::stop_token external_token{};

  bool goal(const CubeState& s) const {
    return phase1_goal ? is_in_g1(s) : is_solved(s);
  }

  int heuristic(const CubeState& s) {
    if (adapter == nullptr) return 0;
    if (phase1_goal) return heuristic_phase1(s, *adapter);
    return heuristic_phase2(s, *adapter);
  }

  bool dfs(const CubeState& cur, int g, int prev_face, int bound) {
    if (abort_kind != 0) return true;
    if (goal(cur)) {
      solution = path;
      found = true;
      return true;
    }
    if (((++nodes) & 1023u) == 0u) {
      if (external_token.stop_requested()) {
        abort_kind = 2;
        return true;
      }
      if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
        abort_kind = 1;
        return true;
      }
    }
    for (const int mv_idx : *allowed) {
      const int face = mv->face[static_cast<std::size_t>(mv_idx)];
      if (face == prev_face) continue;
      // Canonical opposite-face ordering: opposite faces (U/D, R/L,
      // F/B) commute, so keep one order and prune the duplicate.
      // Reordering never changes the reached state or length, hence
      // shortest solutions are preserved.
      if (prev_face >= 0 && face == (prev_face + 3) % 6 && face < prev_face) {
        continue;
      }
      const CubeState nxt = fast_apply(cur, mv_idx, *mv);
      const int h = heuristic(nxt);
      const int f = g + 1 + h;
      if (f > bound) {
        if (f < best_exceed) best_exceed = f;
        continue;
      }
      path.push_back(mv_idx);
      if (dfs(nxt, g + 1, face, bound)) return true;
      path.pop_back();
      if (abort_kind != 0) return true;
    }
    return false;
  }
};

inline std::expected<std::vector<int>, SolveError> run_ida(
    const CubeState& start, bool phase1_goal,
    const std::vector<int>& allowed, int stage_limit, bool has_deadline,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token external_token,
    rubik::tables::TablesAdapter& adapter) {
  const MoveData& md = get_move_data();
  Searcher searcher;
  searcher.mv = &md;
  searcher.allowed = &allowed;
  searcher.phase1_goal = phase1_goal;
  searcher.adapter = &adapter;
  searcher.has_deadline = has_deadline;
  searcher.deadline = deadline;
  searcher.external_token = external_token;
  if (searcher.goal(start)) return std::vector<int>{};
  int bound = searcher.heuristic(start);
  while (true) {
    if (bound > stage_limit) return std::unexpected(SolveError::Timeout);
    if (external_token.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    searcher.best_exceed = std::numeric_limits<int>::max();
    searcher.path.clear();
    searcher.found = false;
    searcher.abort_kind = 0;
    (void)searcher.dfs(start, 0, -1, bound);
    if (searcher.found) return searcher.solution;
    if (searcher.abort_kind == 2) return std::unexpected(SolveError::Cancelled);
    if (searcher.abort_kind == 1) return std::unexpected(SolveError::Timeout);
    if (external_token.stop_requested()) {
      return std::unexpected(SolveError::Cancelled);
    }
    if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected(SolveError::Timeout);
    }
    if (searcher.best_exceed == std::numeric_limits<int>::max()) {
      return std::unexpected(SolveError::Unsolvable);
    }
    bound = searcher.best_exceed;
  }
}

}  // namespace detail

inline std::expected<Solution, SolveError> solve(const CubeState& state,
                                                 const SolveOptions& options) {
  if (options.stop_token.stop_requested()) {
    return std::unexpected(SolveError::Cancelled);
  }
  if (!detail::is_valid_cubie_state(state)) {
    return std::unexpected(SolveError::Unsolvable);
  }
  auto adapter = detail::get_adapter();
  if (adapter == nullptr) return std::unexpected(SolveError::DatabaseMissing);

  const auto t0 = std::chrono::steady_clock::now();
  const bool has_deadline = options.timeout_ms > 0;
  const auto deadline =
      has_deadline ? t0 + std::chrono::milliseconds(options.timeout_ms) : t0;
  int stage_limit = options.max_depth;
  if (stage_limit < 0) stage_limit = 0;
  if (stage_limit < 12) stage_limit = 12;  // two-phase needs room beyond shallow

  const detail::MoveData& md = detail::get_move_data();
  std::vector<int> all18;
  for (int i = 0; i < 18; ++i) all18.push_back(i);
  // Phase-2 move subset: U,U',U2 (0,1,2) + D,D',D2 (9,10,11) + R2,L2,F2,B2
  // (5,14,8,17) in all_face_moves order.
  std::vector<int> phase2_moves = {0, 1, 2, 9, 10, 11, 5, 14, 8, 17};

  // Phase 1 -> G1.
  auto phase1 = detail::run_ida(state, true, all18, stage_limit, has_deadline,
                                deadline, options.stop_token, *adapter);
  if (!phase1) return std::unexpected(phase1.error());

  CubeState after_p1 = state;
  for (const int idx : *phase1) {
    after_p1 = detail::fast_apply(after_p1, idx, md);
  }
  if (!is_in_g1(after_p1)) return std::unexpected(SolveError::Unsolvable);

  // Phase 2 -> solved (inside G1).
  auto phase2 = detail::run_ida(after_p1, false, phase2_moves, stage_limit,
                                has_deadline, deadline, options.stop_token,
                                *adapter);
  if (!phase2) return std::unexpected(phase2.error());

  CubeState after_p2 = after_p1;
  for (const int idx : *phase2) {
    after_p2 = detail::fast_apply(after_p2, idx, md);
  }
  if (!is_solved(after_p2)) return std::unexpected(SolveError::Unsolvable);

  // Optional cancelled/timeout check before reporting.
  if (options.stop_token.stop_requested()) {
    return std::unexpected(SolveError::Cancelled);
  }
  if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
    return std::unexpected(SolveError::Timeout);
  }

  Solution sol;
  StageSolution s1;
  s1.stage_name = "phase1";
  for (const int idx : *phase1) {
    s1.moves.push_back(md.tokens[static_cast<std::size_t>(idx)]);
  }
  StageSolution s2;
  s2.stage_name = "phase2";
  for (const int idx : *phase2) {
    s2.moves.push_back(md.tokens[static_cast<std::size_t>(idx)]);
  }
  sol.stages.push_back(s1);
  sol.stages.push_back(s2);
  return sol;
}

}  // namespace rubik::twophase
