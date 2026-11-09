// tests/tables/tables_verifier.cpp -- independent acceptance verifier for
// tables\ integration (writable-area only; no frozen-interface changes).
// Subcommands:
//   admissibility [seed]          BFS to depth <=6, h <= true distance
//   parity [N] [seed]             500 scrambles depth <=11: length parity +
//                                 with/without-tables benchmark (bench.txt)
//   fallback                      missing/truncated fallback (in-process)
//   selftest                      encoders sanity (solved maps to 0)
//
// All table accesses go through public APIs (twophase::solve,
// TablesAdapter, TableLoader, upstream_* encoders).
// BFS expansion uses fast_apply derived from the frozen core
// (apply(solved, move) once), cross-checked against rubik::apply and the
// independent refsim (tests/core/reference_sim.hpp).
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "core/reference_sim.hpp"
#include "solvers/twophase/solver.hpp"
#include "tables/adapter.hpp"
#include "tables/tables_loader.hpp"

namespace fs = std::filesystem;
using rubik::CubeState;
using rubik::Move;

static const std::string kTablesDir = "H:/rubik-solver/tables";
static const std::string kBenchFile =
    "H:/rubik-solver/tests/tables/bench.txt";
static const std::string kScramblesFile =
    "H:/rubik-solver/tests/tables/parity_scrambles.txt";
static const std::string kParityCsv =
    "H:/rubik-solver/tests/tables/parity.csv";

// ---------------------------------------------------------------- helpers
struct TestMove {
  std::array<std::uint8_t, 8> cp{};
  std::array<std::uint8_t, 8> co{};
  std::array<std::uint8_t, 12> ep{};
  std::array<std::uint8_t, 12> eo{};
};

static std::vector<TestMove> build_test_moves() {
  std::vector<TestMove> out;
  for (const Move& m : rubik::all_face_moves()) {
    const CubeState after = rubik::apply(CubeState::solved(), m);
    TestMove t;
    t.cp = after.cp;
    t.co = after.co;
    t.ep = after.ep;
    t.eo = after.eo;
    out.push_back(t);
  }
  return out;
}

static CubeState fast_apply(const CubeState& s, const TestMove& m) {
  CubeState n = s;
  for (int p = 0; p < 8; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    n.cp[pp] = s.cp[m.cp[pp]];
    n.co[pp] =
        static_cast<std::uint8_t>((s.co[m.cp[pp]] + m.co[pp]) % 3);
  }
  for (int p = 0; p < 12; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    n.ep[pp] = s.ep[m.ep[pp]];
    n.eo[pp] = static_cast<std::uint8_t>(s.eo[m.ep[pp]] ^ m.eo[pp]);
  }
  return n;
}

struct CubeHash {
  std::size_t operator()(const CubeState& s) const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint8_t v) {
      h ^= v;
      h *= 1099511628211ull;
    };
    for (std::uint8_t v : s.cp) mix(v);
    for (std::uint8_t v : s.co) mix(v);
    for (std::uint8_t v : s.ep) mix(v);
    for (std::uint8_t v : s.eo) mix(v);
    return static_cast<std::size_t>(h);
  }
};

static char face_of(const std::string& tok) {
  return tok.empty() ? '?' : tok[0];
}

static std::vector<std::string> random_tokens(std::mt19937& rng, int depth) {
  std::vector<std::string> pool;
  for (const Move& m : rubik::all_face_moves()) pool.push_back(m.token);
  std::uniform_int_distribution<std::size_t> dist(0, pool.size() - 1);
  std::vector<std::string> out;
  for (int i = 0; i < depth; ++i) {
    std::string pick;
    do {
      pick = pool[dist(rng)];
    } while (!out.empty() && face_of(pick) == face_of(out.back()));
    out.push_back(pick);
  }
  return out;
}

static CubeState scramble_state_fast(const std::vector<std::string>& tokens,
                                    const std::vector<TestMove>& moves) {
  CubeState s = CubeState::solved();
  for (const auto& t : tokens) {
    for (std::size_t i = 0; i < moves.size(); ++i) {
      // find move index by token
      // all face moves order matches build order
    }
    // token -> index: rebuild map via all_face_moves order
    static const std::vector<Move> all = rubik::all_face_moves();
    for (std::size_t i = 0; i < all.size(); ++i) {
      if (all[i].token == t) {
        s = fast_apply(s, moves[i]);
        break;
      }
    }
  }
  return s;
}

static CubeState scramble_state_core(const std::vector<std::string>& tokens) {
  std::vector<Move> mv;
  for (const auto& t : tokens) mv.push_back(Move{t});
  return rubik::apply(CubeState::solved(), mv);
}

static std::vector<std::string> solution_tokens(const rubik::Solution& sol) {
  std::vector<std::string> out;
  for (const auto& st : sol.stages) {
    for (const auto& t : st.moves) out.push_back(t);
  }
  return out;
}

static bool verify_solution(const std::vector<std::string>& scramble,
                            const rubik::Solution& sol) {
  std::string f = refsim::apply_sequence(refsim::solved_facelets(), scramble);
  f = refsim::apply_sequence(f, solution_tokens(sol));
  return f == refsim::solved_facelets();
}

static std::string join_tokens(const std::vector<std::string>& toks) {
  std::string out;
  for (std::size_t i = 0; i < toks.size(); ++i) {
    if (i) out += ' ';
    out += toks[i];
  }
  return out;
}

static double median_of(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  if ((n % 2u) != 0u) return v[n / 2u];
  return (v[n / 2u - 1u] + v[n / 2u]) / 2.0;
}
static double percentile_of(std::vector<double> v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  double rank = std::ceil(p / 100.0 * static_cast<double>(v.size()));
  if (rank < 1.0) rank = 1.0;
  if (rank > static_cast<double>(v.size()))
    rank = static_cast<double>(v.size());
  return v[static_cast<std::size_t>(rank - 1.0)];
}
static double mean_of(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double s = 0.0;
  for (double x : v) s += x;
  return s / static_cast<double>(v.size());
}
static double max_of(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  return *std::max_element(v.begin(), v.end());
}

/* ------------------------------------------------------------------ fry */
// fallback: missing + truncated must not crash and must fall back.
static int cmd_fallback() {
  std::cout << "tables_verifier fallback: start\n";
  int failures = 0;
  // 1. missing dir
  {
    rubik::tables::TablesAdapter ad(fs::path("H:/rubik-solver/tests/tables/no_such_dir_x"), false);
    std::cout << "fallback missing: is_valid=" << ad.is_valid()
              << " fell_back=" << ad.fell_back_to_data()
              << " msg=\"" << ad.status_message() << "\"\n";
    if (ad.is_valid()) ++failures;
    const CubeState s = CubeState::solved();
    const int h = ad.combined_h(s, 7, rubik::tables::Metric::HTM);
    std::cout << "fallback missing: combined_h(solved,base=7)=" << h
              << " (want 7)\n";
    if (h != 7) ++failures;
  }
  // 2. truncated single file in a scratch dir under tests/tables/
  {
    const fs::path dir = fs::path("H:/rubik-solver/tests/tables/tmp_trunc");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path src =
        fs::path(kTablesDir) / "cornerHeuristicHTM";
    const fs::path dst = dir / "cornerHeuristicHTM";
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      std::cout << "fallback trunc: copy failed ec=" << ec.message() << "\n";
      ++failures;
    } else {
      const std::uintmax_t full = fs::file_size(dst, ec);
      fs::resize_file(dst, 1000, ec);
      const std::uintmax_t trunc = fs::file_size(dst, ec);
      std::cout << "fallback trunc: copied cornerHeuristicHTM "
                << full << " -> truncated to " << trunc << " bytes\n";
      rubik::tables::TableLoader loader(dir);
      const bool ok = loader.load("cornerHeuristicHTM", false);
      std::cout << "fallback trunc: TableLoader.load(truncated)=" << ok
                << " (want 0)\n";
      if (ok) ++failures;
      if (loader.view("cornerHeuristicHTM") != nullptr) {
        std::cout << "fallback trunc: view non-null on truncated (BAD)\n";
        ++failures;
      }
      rubik::tables::TablesAdapter ad(dir, false);
      std::cout << "fallback trunc: is_valid=" << ad.is_valid()
                << " fell_back=" << ad.fell_back_to_data()
                << " msg=\"" << ad.status_message() << "\"\n";
      if (ad.is_valid()) ++failures;
      const std::string msg = ad.status_message();
      if (msg.find("size mismatch") == std::string::npos &&
          msg.find("missing") == std::string::npos &&
          msg.find("falling back") == std::string::npos) {
        std::cout << "fallback trunc: status msg missing expected words\n";
        ++failures;
      }
    }
    fs::remove_all(dir, ec);
    std::cout << "fallback trunc: temp dir cleaned\n";
  }
  std::cout << "fallback: failures=" << failures << "\n";
  return failures == 0 ? 0 : 1;
}

// selftest: encoders map solved to 0, adapter h(solved)==0.
static int cmd_selftest() {
  const CubeState s = CubeState::solved();
  std::cout << "selftest solved: corner_index="
            << rubik::tables::upstream_corner_index(s)
            << " edge_perm=" << rubik::tables::upstream_edge_perm_index(s)
            << " twist=" << rubik::tables::upstream_twist(s)
            << " flip=" << rubik::tables::upstream_flip(s)
            << " comb495=" << rubik::tables::upstream_comb495(s)
            << " perm24=" << rubik::tables::upstream_perm24(s)
            << " middle=" << rubik::tables::upstream_middle_layer(s)
            << " (want corner0 edge0 twist0 flip0 comb69 middle1656)\n";
  rubik::tables::TablesAdapter ad(fs::path(kTablesDir), false);
  std::cout << "selftest adapter: is_valid=" << ad.is_valid()
            << " msg=\"" << ad.status_message() << "\"\n";
  std::cout << "selftest h(solved): corner=" << ad.h_corner(s)
            << " edge_perm=" << ad.h_edge_perm(s)
            << " phase1=" << ad.h_phase1(s) << " (want 0 0 0)\n";
  const bool ok = (rubik::tables::upstream_corner_index(s) == 0) &&
                  (rubik::tables::upstream_edge_perm_index(s) == 0) &&
                  (rubik::tables::upstream_twist(s) == 0) &&
                  (rubik::tables::upstream_flip(s) == 0) &&
                  (rubik::tables::upstream_middle_layer(s) == 1656u) &&
                  ad.h_corner(s) == 0 && ad.h_edge_perm(s) == 0 &&
                  ad.h_phase1(s) == 0;
  std::cout << "selftest: " << (ok ? "PASS" : "FAIL") << "\n";
  return ok ? 0 : 1;
}

// admissibility: full BFS to depth 6 with fast_apply (cross-checked vs
// core apply + refsim on depth-1 and 1000 random token scrambles).
static int cmd_admissibility(unsigned seed) {
  const auto t_bfs0 = std::chrono::steady_clock::now();
  rubik::tables::TablesAdapter ad(fs::path(kTablesDir), false);
  std::cout << "admissibility adapter: is_valid=" << ad.is_valid()
            << " msg=\"" << ad.status_message() << "\"\n";
  // force lazy loads so view() is valid
  (void)ad.h_corner(CubeState::solved());
  (void)ad.h_edge_perm(CubeState::solved());
  (void)ad.h_phase1(CubeState::solved());
  const std::uint8_t* cornerBase = ad.loader().view("cornerHeuristicHTM");
  const std::uint8_t* edgeBase =
      ad.loader().view("edgePermutationHeuristicHTM");
  const std::uint8_t* p1Base =
      ad.loader().view("kociembaPhase1HeuristicHTM");
  std::cout << "admissibility views: corner=" << (cornerBase != nullptr)
            << " edgePerm=" << (edgeBase != nullptr)
            << " phase1=" << (p1Base != nullptr)
            << " sym=" << (ad.loader().phase1_symmetry_base() != nullptr)
            << "\n";

  const std::vector<TestMove> moves = build_test_moves();
  std::unordered_map<CubeState, int, CubeHash> depth;
  depth.reserve(4000000);
  std::vector<CubeState> frontier{CubeState::solved()};
  depth.emplace(CubeState::solved(), 0);
  std::vector<std::size_t> level_sizes{1};
  // pattern group minima for exactness (corner + edgePerm)
  std::unordered_map<std::uint64_t, int> cornerMin;
  std::unordered_map<std::uint64_t, int> edgeMin;
  cornerMin.reserve(2000000);
  edgeMin.reserve(2000000);
  std::uint64_t violCorner = 0, violEdge = 0, violPhase1 = 0;
  std::uint64_t examined = 0, sentCorner = 0, sentEdge = 0, sentP1 = 0;
  auto check_state = [&](const CubeState& s, int true_d) {
    ++examined;
    const int hc = ad.h_corner(s);
    const int he = ad.h_edge_perm(s);
    const int hp = ad.h_phase1(s);
    if (hc > true_d) ++violCorner;
    if (he > true_d) ++violEdge;
    if (hp > true_d) ++violPhase1;
    if (cornerBase) {
      if (rubik::tables::nibble_get(
              cornerBase, rubik::tables::upstream_corner_index(s)) == 0xFu)
        ++sentCorner;
    }
    if (edgeBase) {
      if (rubik::tables::nibble_get(
              edgeBase, rubik::tables::upstream_edge_perm_index(s)) == 0xFu)
        ++sentEdge;
    }
    (void)p1Base;
    // group minima (first visit in BFS order is minimum)
    const std::uint64_t ci = rubik::tables::upstream_corner_index(s);
    if (cornerMin.find(ci) == cornerMin.end()) cornerMin.emplace(ci, true_d);
    const std::uint64_t ei = rubik::tables::upstream_edge_perm_index(s);
    if (edgeMin.find(ei) == edgeMin.end()) edgeMin.emplace(ei, true_d);
  };
  check_state(CubeState::solved(), 0);
  for (int d = 1; d <= 6; ++d) {
    std::vector<CubeState> next;
    next.reserve(frontier.size() * 4u + 1u);
    for (const CubeState& s : frontier) {
      for (std::size_t mi = 0; mi < moves.size(); ++mi) {
        CubeState ns = fast_apply(s, moves[mi]);
        if (depth.find(ns) == depth.end()) {
          depth.emplace(ns, d);
          next.push_back(ns);
          check_state(ns, d);
        }
      }
    }
    level_sizes.push_back(next.size());
    std::cout << "admissibility BFS depth " << d << ": frontier="
              << next.size() << " total_visited=" << depth.size()
              << " violations(c/e/p)=" << violCorner << "/" << violEdge
              << "/" << violPhase1 << "\n";
    frontier = std::move(next);
    if (frontier.empty()) break;
  }
  // pattern exactness: table raw == group-min BFS depth for every pattern
  std::uint64_t exactC_fail = 0, exactE_fail = 0;
  for (const auto& [idx, mind] : cornerMin) {
    const std::uint8_t raw = rubik::tables::nibble_get(cornerBase, idx);
    if (static_cast<int>(raw) != mind) ++exactC_fail;
  }
  for (const auto& [idx, mind] : edgeMin) {
    const std::uint8_t raw = rubik::tables::nibble_get(edgeBase, idx);
    if (static_cast<int>(raw) != mind) ++exactE_fail;
  }
  std::cout << "admissibility full-BFS: visited=" << depth.size()
            << " examined=" << examined
            << " violCorner=" << violCorner << " violEdge=" << violEdge
            << " violPhase1=" << violPhase1
            << " corner_patterns=" << cornerMin.size()
            << " exactCornerFail=" << exactC_fail
            << " edge_patterns=" << edgeMin.size()
            << " exactEdgeFail=" << exactE_fail
            << " sentinel(c/e/p)=" << sentCorner << "/" << sentEdge << "/"
            << sentP1 << "\n";
  const auto t_bfs1 = std::chrono::steady_clock::now();
  std::cout << "admissibility BFS seconds="
            << std::chrono::duration<double>(t_bfs1 - t_bfs0).count()
            << "\n";

  // depth-1 (all 18) via core apply + refsim cross-check
  std::cout << "admissibility depth-1 (core apply + refsim):\n";
  {
    const std::vector<Move> all = rubik::all_face_moves();
    int d1_viol = 0, d1_ref_fail = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
      const CubeState st = rubik::apply(CubeState::solved(), all[i]);
      const CubeState fst = fast_apply(CubeState::solved(), moves[i]);
      const bool fast_ok = (st == fst);
      const std::string ref =
          refsim::apply_sequence(refsim::solved_facelets(), {all[i].token});
      const bool ref_ok = (rubik::to_facelets(st) == ref);
      const auto it = depth.find(st);
      const int true_d = (it == depth.end() ? -1 : it->second);
      const int hc = ad.h_corner(st);
      const int he = ad.h_edge_perm(st);
      const int hp = ad.h_phase1(st);
      std::cout << "  move=" << all[i].token << " fast_ok=" << fast_ok
                << " refsim_ok=" << ref_ok << " bfs_depth=" << true_d
                << " h(c/e/p)=" << hc << "/" << he << "/" << hp << "\n";
      if (!fast_ok || !ref_ok) ++d1_ref_fail;
      if (true_d != 1 || hc > 1 || he > 1 || hp > 1) ++d1_viol;
    }
    std::cout << "admissibility depth-1: violations=" << d1_viol
              << " refsim/fast mismatches=" << d1_ref_fail << "\n";
    if (d1_viol != 0 || d1_ref_fail != 0) return 1;
  }
  // 1000 random depth<=6 with core apply + refsim + BFS true distance
  std::cout << "admissibility random-1000 (seed=" << seed << "):\n";
  {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> depthDist(0, 6);
    std::uint64_t viol = 0, ref_fail = 0, miss = 0;
    std::uint64_t maxOvershoot = 0;
    for (int i = 0; i < 1000; ++i) {
      const int dscr = depthDist(rng);
      // avoid depth-0 triviality skew: depth uniform 0..6 per spec
      const std::vector<std::string> toks = random_tokens(rng, dscr);
      const CubeState st = scramble_state_core(toks);
      const CubeState fst = scramble_state_fast(toks, moves);
      if (st != fst) {
        ++ref_fail;
        continue;
      }
      const std::string ref =
          refsim::apply_sequence(refsim::solved_facelets(), toks);
      if (rubik::to_facelets(st) != ref) ++ref_fail;
      const auto it = depth.find(st);
      if (it == depth.end()) {
        ++miss;  // unreachable within BFS cap (should not happen depth<=6)
        continue;
      }
      const int true_d = it->second;
      const int hc = ad.h_corner(st);
      const int he = ad.h_edge_perm(st);
      const int hp = ad.h_phase1(st);
      if (hc > true_d || he > true_d || hp > true_d) {
        ++viol;
        const int over = std::max({hc, he, hp}) - true_d;
        if (static_cast<std::uint64_t>(over) > maxOvershoot)
          static_cast<void>(over);
      }
    }
    std::cout << "admissibility random-1000: violations=" << viol
              << " refsim/fast mismatches=" << ref_fail
              << " bfs-misses=" << miss << "\n";
    if (viol != 0 || ref_fail != 0 || miss != 0) return 1;
  }
  const bool ok = (violCorner == 0 && violEdge == 0 && violPhase1 == 0 &&
                   exactC_fail == 0 && exactE_fail == 0);
  std::cout << "admissibility: " << (ok ? "PASS" : "FAIL") << "\n";
  return ok ? 0 : 1;
}


// parity + benchmark: same scrambles, tables vs without.
static int cmd_parity(int n, unsigned seed) {
  const std::vector<TestMove> moves = build_test_moves();
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> depthDist(1, 11);
  std::vector<std::vector<std::string>> scrambles(
      static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    scrambles[static_cast<std::size_t>(i)] =
        random_tokens(rng, depthDist(rng));
  }
  // save scrambles (exact floats: these token lists drive both passes)
  {
    std::ofstream out(kScramblesFile);
    out << "seed=" << seed << " count=" << n << " depth=uniform 1..11\n";
    for (const auto& t : scrambles) out << join_tokens(t) << "\n";
    std::cout << "parity scrambles written to " << kScramblesFile << "\n";
  }
  struct Result {
    bool ok = false;
    int len = -1;
    double ms = 0.0;
    bool verified = false;
    int err = -1;
  };
  std::vector<Result> resWith(static_cast<std::size_t>(n));
  std::vector<Result> resWithout(static_cast<std::size_t>(n));
  auto run_pass = [&](const fs::path& tablesDir, const char* label,
                      std::vector<Result>& out) {
    rubik::tables::set_tables_dir(tablesDir);
    rubik::SolveOptions opts;
    opts.timeout_ms = 60000;
    opts.max_depth = 30;
    opts.thread_count = 4;
    // warmup (untimed, excluded)
    const std::vector<std::string> warm = scrambles[0];
    auto w = rubik::twophase::solve(scramble_state_fast(warm, moves), opts);
    (void)w;
    for (int i = 0; i < n; ++i) {
      const auto& toks = scrambles[static_cast<std::size_t>(i)];
      const CubeState st = scramble_state_fast(toks, moves);
      const auto s0 = std::chrono::steady_clock::now();
      auto r = rubik::twophase::solve(st, opts);
      const auto s1 = std::chrono::steady_clock::now();
      Result rec;
      rec.ms = std::chrono::duration<double, std::milli>(s1 - s0).count();
      if (r) {
        rec.ok = true;
        rec.len = r->total_moves();
        rec.verified = verify_solution(toks, *r);
      } else {
        rec.ok = false;
        rec.err = static_cast<int>(r.error());
      }
      out[static_cast<std::size_t>(i)] = rec;
      if (((i + 1) % 100) == 0) {
        std::cout << "parity " << label << " progress " << (i + 1) << "/"
                  << n << "\n";
      }
    }
  };
  run_pass(fs::path(kTablesDir), "WITH", resWith);
  run_pass(fs::path("H:/rubik-solver/tests/tables/missing_dir_xxx"),
           "WITHOUT", resWithout);
  // parity checks
  int both_ok = 0, len_mismatch = 0, verify_fail = 0, overDepth = 0;
  for (int i = 0; i < n; ++i) {
    const Result& a = resWith[static_cast<std::size_t>(i)];
    const Result& b = resWithout[static_cast<std::size_t>(i)];
    const int depth = static_cast<int>(
        scrambles[static_cast<std::size_t>(i)].size());
    if (a.ok && b.ok) {
      ++both_ok;
      if (a.len != b.len) ++len_mismatch;
      if (!a.verified || !b.verified) ++verify_fail;
      if (a.len > depth || b.len > depth) ++overDepth;
    } else {
      if (!a.verified || !b.verified) {
        if (a.ok || b.ok) ++verify_fail;
      }
    }
  }
  std::vector<double> msWith, msWithout;
  for (const auto& r : resWith) if (r.ok) msWith.push_back(r.ms);
  for (const auto& r : resWithout) if (r.ok) msWithout.push_back(r.ms);
  // save CSV
  {
    std::ofstream csv(kParityCsv);
    csv << "idx,scramble,depth,len_with,ms_with,verified_with,"
           "len_without,ms_without,verified_without\n";
    for (int i = 0; i < n; ++i) {
      const auto& a = resWith[static_cast<std::size_t>(i)];
      const auto& b = resWithout[static_cast<std::size_t>(i)];
      csv << i << ",\"" << join_tokens(scrambles[static_cast<std::size_t>(i)])
          << "\"," << scrambles[static_cast<std::size_t>(i)].size() << ","
          << (a.ok ? std::to_string(a.len) : std::string("ERR")) << ","
          << a.ms << "," << (a.verified ? "yes" : "no") << ","
          << (b.ok ? std::to_string(b.len) : std::string("ERR")) << ","
          << b.ms << "," << (b.verified ? "yes" : "no") << "\n";
    }
  }
  const double medW = median_of(msWith), p95W = percentile_of(msWith, 95.0);
  const double avgW = mean_of(msWith);
  const double medWo = median_of(msWithout),
               p95Wo = percentile_of(msWithout, 95.0);
  const double avgWo = mean_of(msWithout);
  const double speedupMed =
      (medW > 0.0) ? (medWo / medW) : 0.0;
  // bench.txt (measured numbers only)
  {
    std::ofstream out(kBenchFile);
    out << "rubik-solver tables benchmark (twophase, SAME scrambles)\n";
    out << "seed=" << seed << " count=" << n << " depth=uniform 1..11\n";
    out << "tables_dir_with=" << kTablesDir << "\n";
    out << "options: timeout_ms=60000 max_depth=30 thread_count=4\n";
    out << "verify: refsim solved+scramble+solution==solved\n";
    out << "warmup: 1 solve per config (excluded)\n";
    out << "\nconfig | successes | avg_ms | median_ms | p95_ms | max_ms\n";
    out << "WITH tables    | " << msWith.size() << " | " << avgW << " | "
        << medW << " | " << p95W << " | " << max_of(msWith) << "\n";
    out << "WITHOUT tables | " << msWithout.size() << " | " << avgWo << " | "
        << medWo << " | " << p95Wo << " | " << max_of(msWithout) << "\n";
    out << "median speedup (WITHOUT/WITH): " << speedupMed << "x\n";
    out << "parity: both_ok=" << both_ok << " len_mismatch=" << len_mismatch
        << " verify_fail=" << verify_fail
        << " over_scramble_depth=" << overDepth << "\n";
  }
  std::cout << "parity both_ok=" << both_ok << " len_mismatch="
            << len_mismatch << " verify_fail=" << verify_fail
            << " overDepth=" << overDepth << "\n";
  std::cout << "bench WITH: n=" << msWith.size() << " avg=" << avgW
            << " median=" << medW << " p95=" << p95W
            << " max=" << max_of(msWith) << "\n";
  std::cout << "bench WITHOUT: n=" << msWithout.size() << " avg=" << avgWo
            << " median=" << medWo << " p95=" << p95Wo
            << " max=" << max_of(msWithout) << "\n";
  std::cout << "bench median speedup=" << speedupMed << "x\n";
  std::cout << "bench written to " << kBenchFile << "\n";
  const bool ok = (len_mismatch == 0 && verify_fail == 0 && overDepth == 0 &&
                   both_ok == n);
  std::cout << "parity: " << (ok ? "PASS" : "FAIL") << "\n";
  return ok ? 0 : 1;
}

// discovery: priority explicit > ./tables > build/tables (mock dirs, no
// 11.8 GB tables needed). Uses the pure helpers so no cwd change or
// global override is involved.
static int cmd_discovery() {
  std::cout << "discovery: start (mock dirs)\n";
  int failures = 0;
  auto check = [&](bool cond, const std::string& what) {
    std::cout << "  " << what << ": " << (cond ? "ok" : "FAIL") << "\n";
    if (!cond) ++failures;
  };
  std::error_code ec;
  const fs::path root =
      fs::temp_directory_path(ec) / "rubik_tables_discovery_mock";
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  const fs::path cwd = root / "work";
  const fs::path exe_dir = root / "exe";
  const fs::path local = cwd / "tables";          // (2) ./tables
  const fs::path bld = cwd / "build" / "tables";  // (3) build/tables
  const fs::path exe_t = exe_dir / "tables";      // (4)
  const fs::path explicit_dir = root / "explicit";
  auto make_tables = [&](const fs::path& d) {
    fs::create_directories(d, ec);
    std::ofstream(d / "cornerHeuristicHTM") << "mock";
  };
  // 0. Empty: no candidate wins (fallback would be h=0 Timeout).
  {
    const auto got = rubik::tables::first_tables_dir(
        rubik::tables::tables_discovery_candidates(cwd, exe_dir));
    check(got.empty(), "empty -> empty");
  }
  // 1. Explicit wins over ./tables + build/tables.
  make_tables(local);
  make_tables(bld);
  make_tables(exe_t);
  make_tables(explicit_dir);
  {
    const auto got = rubik::tables::discover_tables_dir(explicit_dir, cwd,
                                                         exe_dir);
    check(got == explicit_dir, "explicit wins");
    check(rubik::tables::dir_contains_tables(explicit_dir),
          "explicit dir_contains_tables");
  }
  // 2. ./tables wins over build/tables.
  {
    const auto got = rubik::tables::discover_tables_dir(fs::path(), cwd,
                                                         exe_dir);
    check(got == local, "./tables wins over build/tables");
  }
  // 3. build/tables wins when ./tables is absent.
  fs::remove_all(local, ec);
  {
    const auto got = rubik::tables::discover_tables_dir(fs::path(), cwd,
                                                         exe_dir);
    check(got == bld, "build/tables when ./tables absent");
  }
  // 4. exe tables win when cwd candidates are absent.
  fs::remove_all(bld, ec);
  {
    const auto got = rubik::tables::discover_tables_dir(fs::path(), cwd,
                                                         exe_dir);
    check(got == exe_t, "exe tables when cwd absent");
  }
  // 5. resolve-with-explicit still wins (global API).
  {
    const auto got =
        rubik::tables::resolve_tables_dir(explicit_dir);
    check(got == explicit_dir, "resolve_tables_dir(explicit) wins");
  }
  fs::remove_all(root, ec);
  std::cout << "discovery: failures=" << failures << "\n";
  std::cout << "discovery: " << (failures == 0 ? "PASS" : "FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "Usage: tables_verifier <fallback|selftest|admissibility|"
                 "discovery|parity> [args]\n";
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "fallback") return cmd_fallback();
  if (cmd == "discovery") return cmd_discovery();
  if (cmd == "selftest") return cmd_selftest();
  if (cmd == "admissibility") {
    unsigned seed = 20261108u;
    if (argc >= 3) seed = static_cast<unsigned>(std::stoul(argv[2]));
    return cmd_admissibility(seed);
  }
  if (cmd == "parity") {
    int n = 500;
    unsigned seed = 888002u;
    if (argc >= 3) n = std::stoi(argv[2]);
    if (argc >= 4) seed = static_cast<unsigned>(std::stoul(argv[3]));
    return cmd_parity(n, seed);
  }
  std::cout << "unknown subcommand: " << cmd << "\n";
  return 2;
}
