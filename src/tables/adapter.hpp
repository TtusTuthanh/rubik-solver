#pragma once
// Adapter: plugs tables\ heuristics into the frozen HTM max()
// heuristic WITHOUT changing the frozen solve() interface
// (docs/tables.md Section 4 + Section 7). The integrator calls
// combined_h(state, base_h) per node inside its existing
// heuristic_raw(); frozen signatures and SolveOptions are untouched.
//
// VERIFICATION GATE: encoders now use exact upstream constants
// (cubie-indexed PieceCube, upstream flip scatter; see tables_loader).
// BFS admissibility probe (depth-1 all 18 moves + 1000 random
// depth<=6, core apply cross-checked vs reference simulator) passes:
// depth-1 fail 0/18, random violations 0/1000, and twist/flip/middle
// successors at solved match kociembaPhase1MovementHTM exactly.
// is_valid() remains file-validity (sizes/hashes); encoder
// admissibility is established by that probe, not by is_valid().
//
// HTM is the only metric admissible for the frozen 18-face-move HTM
// solver; QTM/STM/ATM table values are exposed for completeness (the
// "all metrics" approval) but MUST NOT be max()'d into an HTM
// heuristic (they can overestimate). combined_h() therefore defaults
// to HTM. Two-phase (phase2 + symmetry + movements) supports the
// separately-approved new two-phase solver method and is also exposed
// here (parity-halved 2-bit heuristic + movement successors).

#include <cstdint>
#include <filesystem>
#include <string>

#include "core/cube.hpp"
#include "tables/tables_loader.hpp"

namespace rubik::tables {

// Heuristic adapter owning a TableLoader plus a fallback verdict:
// when the tables directory is missing/invalid, is_valid() is false,
// fell_back_to_data() is true, and combined_h() returns base_h
// unchanged while status_message() explains the fallback to data DBs.
// No heuristic ever fails closed by crashing; sentinels and
// out-of-range indexes map to 0 (admissible).
class TablesAdapter {
 public:
  // dir empty = resolved default. verify_hashes hashes every file
  // lazily loaded (fail-closed); size is always enforced. Default
  // verify_hashes=false to avoid multi-GB hashing on startup; the
  // self-test enables it. Hashing 4.3 GB takes minutes.
  explicit TablesAdapter(std::filesystem::path dir = std::filesystem::path(),
                         bool verify_hashes = false);
  TablesAdapter(const TablesAdapter&) = delete;
  TablesAdapter& operator=(const TablesAdapter&) = delete;
  TablesAdapter(TablesAdapter&&) noexcept;
  TablesAdapter& operator=(TablesAdapter&&) noexcept;
  ~TablesAdapter() = default;

  bool is_valid() const noexcept { return status_.ok && status_.using_tables; }
  bool using_tables() const noexcept { return status_.using_tables; }
  bool fell_back_to_data() const noexcept { return status_.fell_back_to_data; }
  std::string status_message() const { return status_.message; }
  const LoadStatus& status() const noexcept { return status_; }
  const std::filesystem::path& dir() const noexcept { return loader_.dir(); }
  TableLoader& loader() noexcept { return loader_; }
  const TableLoader& loader() const noexcept { return loader_; }

  // Individual admissible heuristics (HTM defaults). Each returns 0
  // when its file is not loaded, so max() stays admissible; check
  // is_valid() / loader().is_loaded() to detect absence.
  int h_corner(const CubeState& state, Metric metric = Metric::HTM) const noexcept;
  int h_edge_perm(const CubeState& state, Metric metric = Metric::HTM) const noexcept;
  int h_phase1(const CubeState& state, Metric metric = Metric::HTM) const noexcept;
  int h_phase1_large(const CubeState& state, Metric metric = Metric::HTM) const noexcept;
  // Phase-2 (2-bit, mod-3) heuristic: meaningful only inside G1 for a
  // two-phase solver; still admissible (0 on sentinel/missing).
  int h_phase2_from_coords(std::uint32_t class2768, std::uint32_t ud_perm,
                           std::uint32_t slice_perm, Metric metric = Metric::HTM) const noexcept;

  // max() combinator for the frozen HTM solver: max of base_h
  // (four data\ PDBs) with tables\ cornerHTM + edgePermHTM +
  // phase1HTM. Pass metric=HTM (default). Non-HTM metrics are
  // documented inadmissible for HTM distance and only included when
  // the caller explicitly selects them (e.g. a future multi-metric
  // solver); the frozen solver must keep HTM.
  int combined_h(const CubeState& state, int base_h,
                 Metric metric = Metric::HTM) const noexcept;

 private:
  TableLoader loader_;
  LoadStatus status_;
  bool verify_hashes_ = false;
};

// Standalone self-test (see tables_selftest.cpp): size (+ optional
// hash) check of every file in dir against docs/tables.md, encoders
// sanity (solved maps to 0), then fallback verdict. Returns 0 when
// tables are valid, 1 when tables are missing/invalid and fallback
// applies, 2 on unexpected internal error. Prints to std::cout.
int run_self_test(const std::filesystem::path& dir, bool verify_hashes);

}  // namespace rubik::tables
