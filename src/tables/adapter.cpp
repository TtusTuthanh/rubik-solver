#include "tables/adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace rubik::tables {

TablesAdapter::TablesAdapter(std::filesystem::path dir, bool verify_hashes)
    : loader_(dir.empty() ? resolve_tables_dir() : dir),
      status_(describe_tables_dir(loader_.dir(), verify_hashes)),
      verify_hashes_(verify_hashes) {
  (void)verify_hashes_;
}

TablesAdapter::TablesAdapter(TablesAdapter&& other) noexcept
    : loader_(std::move(other.loader_)),
      status_(std::move(other.status_)),
      verify_hashes_(other.verify_hashes_) {}

TablesAdapter& TablesAdapter::operator=(TablesAdapter&& other) noexcept {
  if (this == &other) return *this;
  loader_ = std::move(other.loader_);
  status_ = std::move(other.status_);
  verify_hashes_ = other.verify_hashes_;
  return *this;
}

namespace {
std::string corner_h_name_local(Metric m) {
  return "cornerHeuristic" + std::string(metric_suffix(m));
}
std::string edge_h_name_local(Metric m) {
  return "edgePermutationHeuristic" + std::string(metric_suffix(m));
}
std::string phase1_h_name_local(Metric m) {
  return "kociembaPhase1Heuristic" + std::string(metric_suffix(m));
}
std::string phase1_large_h_name_local(Metric m) {
  return "largeKociembaPhase1Heuristic" + std::string(metric_suffix(m));
}
std::string phase2_h_name_local(Metric m) {
  return "kociembaPhase2Heuristic" + std::string(metric_suffix(m));
}
}  // namespace

int TablesAdapter::h_corner(const CubeState& state, Metric metric) const noexcept {
  if (!loader_.is_loaded(corner_h_name_local(metric))) {
    const_cast<TableLoader&>(loader_).load(corner_h_name_local(metric),
                                           verify_hashes_);
  }
  const std::string name = corner_h_name_local(metric);
  const std::uint8_t* base = loader_.view(name);
  if (base == nullptr) return 0;
  return nibble_heuristic(base, upstream_corner_index(state), kCornerEntries);
}

int TablesAdapter::h_edge_perm(const CubeState& state, Metric metric) const noexcept {
  if (!loader_.is_loaded(edge_h_name_local(metric))) {
    const_cast<TableLoader&>(loader_).load(edge_h_name_local(metric),
                                           verify_hashes_);
  }
  const std::string name = edge_h_name_local(metric);
  const std::uint8_t* base = loader_.view(name);
  if (base == nullptr) return 0;
  return nibble_heuristic(base, upstream_edge_perm_index(state), kEdgePermEntries);
}

int TablesAdapter::h_phase1(const CubeState& state, Metric metric) const noexcept {
  if (!loader_.is_loaded("kociembaPhase1Symmetry")) {
    const_cast<TableLoader&>(loader_).load("kociembaPhase1Symmetry",
                                           verify_hashes_);
  }
  if (!loader_.is_loaded(phase1_h_name_local(metric))) {
    const_cast<TableLoader&>(loader_).load(phase1_h_name_local(metric),
                                           verify_hashes_);
  }
  const std::string name = phase1_h_name_local(metric);
  const std::uint8_t* base = loader_.view(name);
  if (base == nullptr || loader_.phase1_symmetry_base() == nullptr) {
    return 0;
  }
  const std::uint32_t twist = upstream_twist(state);
  const std::uint32_t flip = upstream_flip(state);
  const std::uint32_t comb = upstream_comb495(state);
  const std::uint32_t middle = upstream_middle_layer(state);
  const std::uint32_t sym = loader_.phase1_min_orientation(comb);
  const std::uint32_t mid_sym = loader_.phase1_middle_sym(middle, sym);
  const std::uint32_t class45 = loader_.phase1_equiv_class(mid_sym);
  const std::uint32_t flip_sym = loader_.phase1_edge_sym(flip, comb, sym);
  const std::uint32_t twist_sym = loader_.phase1_corner_sym(twist, sym);
  const std::uint64_t idx = phase1_reduced_index(class45, flip_sym, twist_sym);
  return nibble_heuristic(base, idx, kPhase1ReducedEntries);
}

int TablesAdapter::h_phase1_large(const CubeState& state, Metric metric) const noexcept {
  if (!loader_.is_loaded("kociembaPhase1Symmetry")) {
    const_cast<TableLoader&>(loader_).load("kociembaPhase1Symmetry",
                                           verify_hashes_);
  }
  if (!loader_.is_loaded(phase1_large_h_name_local(metric))) {
    const_cast<TableLoader&>(loader_).load(phase1_large_h_name_local(metric),
                                           verify_hashes_);
  }
  const std::string name = phase1_large_h_name_local(metric);
  const std::uint8_t* base = loader_.view(name);
  if (base == nullptr || loader_.phase1_symmetry_base() == nullptr) {
    return 0;
  }
  const std::uint32_t twist = upstream_twist(state);
  const std::uint32_t flip = upstream_flip(state);
  const std::uint32_t comb = upstream_comb495(state);
  const std::uint32_t middle = upstream_middle_layer(state);
  const std::uint32_t sym = loader_.phase1_min_orientation(comb);
  const std::uint32_t mid_sym = loader_.phase1_middle_sym(middle, sym);
  const std::uint32_t class45 = loader_.phase1_equiv_class(mid_sym);
  const std::uint32_t flip_sym = loader_.phase1_edge_sym(flip, comb, sym);
  const std::uint32_t twist_sym = loader_.phase1_corner_sym(twist, sym);
  const std::uint64_t idx =
      phase1_large_index(class45, flip_sym, twist_sym, mid_sym);
  return nibble_heuristic(base, idx, kPhase1LargeEntries);
}

int TablesAdapter::h_phase2_from_coords(std::uint32_t class2768,
                                        std::uint32_t ud_perm,
                                        std::uint32_t slice_perm,
                                        Metric metric) const noexcept {
  if (!loader_.is_loaded(phase2_h_name_local(metric))) {
    const_cast<TableLoader&>(loader_).load(phase2_h_name_local(metric),
                                           verify_hashes_);
  }
  const std::string name = phase2_h_name_local(metric);
  const std::uint8_t* base = loader_.view(name);
  if (base == nullptr) return 0;
  const std::uint64_t idx = phase2_index(class2768, ud_perm, slice_perm);
  return two_bit_heuristic(base, idx, kPhase2Entries);
}

int TablesAdapter::combined_h(const CubeState& state, int base_h,
                              Metric metric) const noexcept {
  if (!is_valid()) return base_h;
  // HTM is the only metric admissible for the frozen HTM solver; the
  // caller may select others only for a future multi-metric solver.
  // Guard the admissible path explicitly so non-HTM can never leak
  // into the frozen search by accident: non-HTM reduces to base_h
  // unless the caller is a two-phase/multi-metric consumer (which
  // must call h_* directly with its own metric).
  if (metric != Metric::HTM) return base_h;
  // Phase-1 heuristic is distance-to-G1, admissible for every state
  // (not only inside G1); phase-2 is deliberately excluded here
  // because it is meaningful only inside G1 for a two-phase solver.
  int h = base_h;
  h = std::max(h, h_corner(state, metric));
  h = std::max(h, h_edge_perm(state, metric));
  h = std::max(h, h_phase1(state, metric));
  return h;
}

int run_self_test(const std::filesystem::path& dir, bool verify_hashes) {
  std::cout << "tables self-test: dir=" << dir.string()
            << " verify_hashes=" << (verify_hashes ? "yes" : "no") << "\n";
  if (!is_little_endian()) {
    std::cout << "tables self-test: FAIL host is not little-endian\n";
    std::cout
        << "tables\\ missing/invalid: falling back to data\\ DBs\n";
    return 2;
  }
  const LoadStatus st = describe_tables_dir(dir, verify_hashes);
  std::cout << "tables self-test: " << st.message << "\n";
  std::cout << "tables self-test: using_tables="
            << (st.using_tables ? "yes" : "no") << " fell_back_to_data="
            << (st.fell_back_to_data ? "yes" : "no") << "\n";

  TableLoader loader(dir);
  std::size_t ok_size = 0;
  std::size_t ok_hash = 0;
  for (const auto& spec : kTableSpecs) {
    const std::filesystem::path path = dir / std::string(spec.filename);
    std::error_code ec;
    const std::uintmax_t fsize = std::filesystem::file_size(path, ec);
    std::string verdict;
    bool size_ok = false;
    if (ec) {
      verdict = "MISSING";
    } else if (fsize != spec.expected_bytes) {
      verdict = "SIZE_MISMATCH got=" + std::to_string(fsize) +
                " want=" + std::to_string(spec.expected_bytes);
    } else {
      size_ok = true;
      verdict = "SIZE_OK";
    }
    if (size_ok) {
      ++ok_size;
      if (verify_hashes) {
        if (sha256_file_matches(path, spec.sha256_hex)) {
          ++ok_hash;
          verdict += " HASH_OK";
        } else {
          verdict += " HASH_MISMATCH";
        }
      }
    }
    std::cout << "  " << spec.filename << ": " << verdict << "\n";
  }
  std::cout << "tables self-test: size_ok " << ok_size << "/" << kTableSpecs.size()
            << "\n";
  if (verify_hashes) {
    std::cout << "tables self-test: hash_ok " << ok_hash << "/"
              << kTableSpecs.size() << "\n";
  }

  // Encoder sanity: solved maps to 0 on every upstream coordinate.
  const CubeState solved = CubeState::solved();
  const std::uint64_t ci = upstream_corner_index(solved);
  const std::uint64_t ei = upstream_edge_perm_index(solved);
  const std::uint32_t tw = upstream_twist(solved);
  const std::uint32_t fl = upstream_flip(solved);
  const std::uint32_t c495 = upstream_comb495(solved);
  const std::uint32_t p24 = upstream_perm24(solved);
  const std::uint32_t mid = upstream_middle_layer(solved);
  std::cout << "tables self-test: solved corner_index=" << ci
            << " edge_perm=" << ei << " twist=" << tw << " flip=" << fl
            << " comb495=" << c495 << " perm24=" << p24
            << " middle=" << mid << " (want 69*24=1656)\n";
  std::cout << "tables self-test: solved corner=" << ci
            << " (want 0)\n";
  const std::array<std::uint8_t, 8> raw = upstream_unordered_corners(solved);
  std::cout << "tables self-test: raw unorder gather for solved [";
  for (std::size_t i = 0; i < raw.size(); ++i) {
    std::cout << (i ? "," : "") << static_cast<unsigned>(raw[i]);
  }
  std::cout << "] (want [1,5,0,4,3,7,2,6]; heuristic uses cubie-indexed PieceCube)\n";
  // Load the HTM set and smoke one lookup per family (size/hash path).
  const std::size_t loaded = loader.load_all(verify_hashes);
  std::cout << "tables self-test: loaded " << loaded << "/"
            << kTableSpecs.size() << " files\n";
  if (st.using_tables) {
    TablesAdapter adapter(dir, verify_hashes);
    const int hc = adapter.h_corner(solved, Metric::HTM);
    const int he = adapter.h_edge_perm(solved, Metric::HTM);
    const int hp = adapter.h_phase1(solved, Metric::HTM);
    std::cout << "tables self-test: solved h_corner=" << hc
              << " h_edge_perm=" << he << " h_phase1=" << hp
              << " (want 0)\n";
    if (hc != 0 || he != 0 || hp != 0) {
      std::cout
          << "tables self-test: FAIL solved heuristic nonzero (index bug)\n";
      return 2;
    }
    if (ci != 0 || ei != 0 || tw != 0 || fl != 0 || mid != 1656u) {
      std::cout << "tables self-test: FAIL solved encoder nonzero "
                   "(see open issues: cubie map unverified)\n";
      return 2;
    }
    std::cout << "tables self-test: PASS tables valid\n";
    return 0;
  }
  std::cout
      << "tables\\ invalid/missing: falling back to data\\ DBs (optimal PDBs)\n";
  std::cout << "tables self-test: PASS (fallback verdict, not a crash)\n";
  return 1;
}

}  // namespace rubik::tables
