// benchmarks/fuzz.cpp
//
// Standalone fuzz/benchmark executable (NOT part of ctest; main agent owns
// CMake wiring). See docs/SPEC.md sections 10 + 8.
//
// In a single process it loops over each method (cfop, roux, zz, heise at
// scramble depth 25), generates N random scrambles from the 18 face moves
// with fixed seeds, solves via rubik::<method>::solve, verifies every
// solution with the independent reference simulator
// (tests/core/reference_sim.hpp : solved + scramble + solution == solved),
// and records success/failure/timeout counts, total_moves stats
// (min/max/avg) and elapsed ms (avg/median/p95/max).
//
// Output: the same report is printed to stdout and written to
// benchmarks/results.txt (override with --output). Final results go to
// stdout; periodic progress goes to stderr so the table stays easy to
// extract. Human solvers themselves log one line per stage to stdout, so
// raw stdout will interleave those lines with the final table.
//
// Build (from repo root, MSYS2 UCRT64 g++ 16.2) - see report for the full
// command. Single-line g++ invocation with "-std=c++23 -O2
// -Wall -Wextra -Wpedantic -I src -I tests benchmarks/fuzz.cpp
// src/core/cube.cpp src/core/moves.cpp src/core/validation.cpp
// src/solvers/cfop/solver.cpp src/solvers/roux/solver.cpp
// src/solvers/zz/solver.cpp src/solvers/heise/solver.cpp
// -o benchmarks/fuzz  (MSYS2 bash).
// PowerShell uses the same inputs with "C:\msys64\ucrt64\bin\g++.exe"
// and -o benchmarks/fuzz.exe.
//
// Usage (defaults: trials=1000, all methods, seed-base=20261108):
//   ./fuzz [--trials N] [--methods cfop,roux,zz,heise]
//          [--seed-base N] [--human-depth 25]
//          [--human-timeout-ms 30000]
//          [--human-max-depth 30]
//          [--output benchmarks/results.txt]
//          [--time-budget-sec S]   (0 = no limit; use to cap a reduced run)
//
// Exit code: 0 when every requested method with trials>0 reaches >=99%
// verified success; 1 otherwise; 2 on CLI misuse.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"
#include "core/moves.hpp"
#include "core/reference_sim.hpp"
#include "solvers/cfop/solver.hpp"
#include "solvers/heise/solver.hpp"
#include "solvers/roux/solver.hpp"
#include "solvers/zz/solver.hpp"

namespace {

using rubik::CubeState;
using rubik::Move;
using rubik::SolveError;
using rubik::SolveOptions;
using rubik::Solution;

struct Config {
  int trials{1000};
  unsigned seed_base{20261108u};
  int human_depth{25};
  int human_timeout_ms{30000};
  int human_max_depth{30};
  std::string output{"benchmarks/results.txt"};
  std::string methods{"cfop,roux,zz,heise"};
  double time_budget_sec{0.0};  // 0 = unlimited
  bool help{false};
};

struct MethodCfg {
  std::string name;
  int scramble_depth{25};
  int timeout_ms{30000};
  int max_depth{30};
  int thread_count{1};
  unsigned seed_offset{0u};
  rubik::SolverFn fn{nullptr};
};

struct FailureRecord {
  int trial{0};
  std::string scramble;
  std::string reason;
  double ms{0.0};
};

struct MethodStats {
  std::string name;
  int scramble_depth{0};
  int timeout_ms{0};
  int max_depth{0};
  unsigned seed{0u};
  int requested{0};
  int attempted{0};
  int successes{0};
  int failures{0};
  int timeouts{0};
  int database_missing{0};
  int unsolvable{0};
  int cancelled{0};
  int other_errors{0};
  int verify_failures{0};
  bool skipped_database_missing{false};
  bool incomplete{false};
  double warmup_ms{0.0};
  std::string warmup_error;
  std::vector<double> success_ms;
  std::vector<int> success_moves;
  std::vector<FailureRecord> failures_detail;
  // Slowest verified solve (warmup excluded).
  double slowest_ms{0.0};
  int slowest_moves{0};
  int slowest_trial{0};
  std::string slowest_scramble;
  double method_wall_sec{0.0};
};

std::string error_name(SolveError e) {
  switch (e) {
  case SolveError::Unsolvable: return "Unsolvable";
  case SolveError::Timeout: return "Timeout";
  case SolveError::Cancelled: return "Cancelled";
  case SolveError::DatabaseMissing: return "DatabaseMissing";
  case SolveError::InvalidInput: return "InvalidInput";
  case SolveError::NotImplemented: return "NotImplemented";
  }
  return "Unknown";
}

bool parse_args(int argc, char* argv[], Config& cfg, std::string& err) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    auto need_value = [&](std::string_view flag) -> std::string {
      if (i + 1 >= argc) {
        err = std::format("missing value for {}", flag);
        return {};
      }
      return argv[++i];
    };
    try {
      if (a == "--help" || a == "-h") {
        cfg.help = true;
      } else if (a == "--trials") {
        cfg.trials = std::stoi(need_value(a));
      } else if (a == "--seed-base") {
        cfg.seed_base = static_cast<unsigned>(std::stoul(need_value(a)));
      } else if (a == "--human-depth") {
        cfg.human_depth = std::stoi(need_value(a));
      } else if (a == "--human-timeout-ms") {
        cfg.human_timeout_ms = std::stoi(need_value(a));
      } else if (a == "--human-max-depth") {
        cfg.human_max_depth = std::stoi(need_value(a));
      } else if (a == "--output") {
        cfg.output = need_value(a);
      } else if (a == "--methods") {
        cfg.methods = need_value(a);
      } else if (a == "--time-budget-sec") {
        cfg.time_budget_sec = std::stod(need_value(a));
      } else {
        err = std::format("unknown argument: {}", a);
        return false;
      }
    } catch (const std::exception& e) {
      err = std::format("bad value for {}: {}", a, e.what());
      return false;
    }
  }
  if (cfg.trials <= 0) {
    err = "trials must be > 0";
    return false;
  }
  if (cfg.human_depth < 0) {
    err = "scramble depth must be >= 0";
    return false;
  }
  if (cfg.human_timeout_ms <= 0) {
    err = "timeout must be > 0";
    return false;
  }
  return err.empty();
}

std::vector<std::string> split_methods(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == ',') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else if (c != ' ' && c != '\t') {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  for (auto& s : out) {
    for (auto& c : s) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

void print_help() {
  std::cout << "Usage: fuzz [--trials N] [--methods cfop,roux,zz,heise]\n"
               "            [--seed-base N] [--human-depth 25]\n"
               "            [--human-timeout-ms 30000]\n"
               "            [--human-max-depth 30]\n"
               "            [--output benchmarks/results.txt]\n"
               "            [--time-budget-sec S] (0 = no limit)\n";
}

char face_of(const std::string& tok) { return tok.empty() ? '?' : tok[0]; }

std::vector<std::string> random_scramble_tokens(std::mt19937& rng, int depth) {
  const std::vector<Move> face_moves = rubik::all_face_moves();
  std::vector<std::string> pool;
  pool.reserve(face_moves.size());
  for (const Move& m : face_moves) pool.push_back(m.token);
  std::uniform_int_distribution<std::size_t> dist(0, pool.size() - 1);
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(depth > 0 ? depth : 0));
  for (int i = 0; i < depth; ++i) {
    std::string pick;
    do {
      pick = pool[dist(rng)];
    } while (!out.empty() && face_of(pick) == face_of(out.back()));
    out.push_back(pick);
  }
  return out;
}

CubeState scramble_state(const std::vector<std::string>& tokens) {
  std::vector<Move> moves;
  moves.reserve(tokens.size());
  for (const std::string& t : tokens) moves.push_back(Move{t});
  return rubik::apply(CubeState::solved(), moves);
}

std::vector<std::string> solution_tokens(const Solution& sol) {
  std::vector<std::string> out;
  for (const auto& stage : sol.stages) {
    for (const std::string& t : stage.moves) out.push_back(t);
  }
  return out;
}

std::string join_tokens(const std::vector<std::string>& tokens) {
  std::string out;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i != 0) out += ' ';
    out += tokens[i];
  }
  return out;
}

// Reference-simulator check: solved + scramble + solution == solved.
bool verify_solution(const std::vector<std::string>& scramble,
                     const Solution& sol) {
  std::string f = refsim::apply_sequence(refsim::solved_facelets(), scramble);
  f = refsim::apply_sequence(f, solution_tokens(sol));
  return f == refsim::solved_facelets();
}

double percentile_sorted(const std::vector<double>& sorted, double p) {
  if (sorted.empty()) return 0.0;
  if (sorted.size() == 1) return sorted.front();
  // Nearest-rank: rank = ceil(p/100 * n), 1-based.
  double rank = std::ceil(p / 100.0 * static_cast<double>(sorted.size()));
  if (rank < 1.0) rank = 1.0;
  if (rank > static_cast<double>(sorted.size())) rank = static_cast<double>(sorted.size());
  return sorted[static_cast<std::size_t>(rank - 1.0)];
}

double median_of(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  if ((n % 2u) != 0u) return v[n / 2u];
  return (v[n / 2u - 1u] + v[n / 2u]) / 2.0;
}

double mean_of_double(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double mean_of_int(const std::vector<int>& v) {
  if (v.empty()) return 0.0;
  const double sum =
      std::accumulate(v.begin(), v.end(), 0.0);
  return sum / static_cast<double>(v.size());
}

int min_of(const std::vector<int>& v) {
  if (v.empty()) return 0;
  return *std::min_element(v.begin(), v.end());
}

int max_of(const std::vector<int>& v) {
  if (v.empty()) return 0;
  return *std::max_element(v.begin(), v.end());
}

double percentile_of(std::vector<double> v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return percentile_sorted(v, p);
}

double max_double(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  return *std::max_element(v.begin(), v.end());
}

}  // namespace

int main(int argc, char* argv[]) {
  Config cfg;
  std::string err;
  if (!parse_args(argc, argv, cfg, err)) {
    std::cerr << std::format("Error: {}\n", err);
    print_help();
    return 2;
  }
  if (cfg.help) {
    print_help();
    return 0;
  }

  const std::vector<std::string> wanted = split_methods(cfg.methods);
  const std::vector<MethodCfg> all_methods = {
      {"cfop", cfg.human_depth, cfg.human_timeout_ms, cfg.human_max_depth, 1,
       0u, &rubik::cfop::solve},
      {"roux", cfg.human_depth, cfg.human_timeout_ms, cfg.human_max_depth, 1,
       100000u, &rubik::roux::solve},
      {"zz", cfg.human_depth, cfg.human_timeout_ms, cfg.human_max_depth, 1,
       200000u, &rubik::zz::solve},
      {"heise", cfg.human_depth, cfg.human_timeout_ms, cfg.human_max_depth, 1,
       300000u, &rubik::heise::solve},
  };

  std::vector<MethodCfg> methods;
  for (const auto& m : all_methods) {
    if (wanted.empty()) {
      methods.push_back(m);
      continue;
    }
    bool keep = false;
    for (const auto& w : wanted) {
      if (w == m.name) keep = true;
    }
    if (keep) methods.push_back(m);
  }
  if (methods.empty()) {
    std::cerr << "Error: no methods selected\n";
    print_help();
    return 2;
  }

  const auto global_t0 = std::chrono::steady_clock::now();
  std::vector<MethodStats> stats;
  stats.reserve(methods.size());

  for (const auto& mc : methods) {
    MethodStats st;
    st.name = mc.name;
    st.scramble_depth = mc.scramble_depth;
    st.timeout_ms = mc.timeout_ms;
    st.max_depth = mc.max_depth;
    st.seed = cfg.seed_base + mc.seed_offset;
    st.requested = cfg.trials;

    const auto method_t0 = std::chrono::steady_clock::now();

    SolveOptions opts;
    opts.timeout_ms = mc.timeout_ms;
    opts.max_depth = mc.max_depth;
    opts.thread_count = mc.thread_count;

    // Warmup (not counted): one solve so lazy move-table builds and
    // table loads do not inflate trial timing stats.
    {
      std::mt19937 warm_rng(st.seed + 999999u);
      const std::vector<std::string> warm_scramble =
          random_scramble_tokens(warm_rng, 5);
      const CubeState warm_state = scramble_state(warm_scramble);
      const auto w0 = std::chrono::steady_clock::now();
      auto warm = mc.fn(warm_state, opts);
      const auto w1 = std::chrono::steady_clock::now();
      st.warmup_ms = std::chrono::duration<double, std::milli>(w1 - w0).count();
      if (!warm) {
        st.warmup_error = error_name(warm.error());
        std::cerr << std::format("[fuzz] {} warmup failed: {} ({:.1f} ms)\n",
                                 mc.name, st.warmup_error, st.warmup_ms);
      } else {
        std::cerr << std::format("[fuzz] {} warmup ok: {} moves in {:.1f} ms\n",
                                 mc.name, warm->total_moves(), st.warmup_ms);
      }
      if (warm && warm.error() == SolveError::DatabaseMissing) {
        st.skipped_database_missing = true;
        const auto method_t1 = std::chrono::steady_clock::now();
        st.method_wall_sec =
            std::chrono::duration<double>(method_t1 - method_t0).count();
        stats.push_back(std::move(st));
        continue;
      }
    }

    std::mt19937 rng(st.seed);
    bool budget_hit = false;
    for (int trial = 0; trial < cfg.trials; ++trial) {
      if (cfg.time_budget_sec > 0.0) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
            std::chrono::duration<double>(now - global_t0).count();
        if (elapsed > cfg.time_budget_sec) {
          budget_hit = true;
          break;
        }
      }

      const std::vector<std::string> scramble =
          random_scramble_tokens(rng, mc.scramble_depth);
      const CubeState state = scramble_state(scramble);

      const auto t0 = std::chrono::steady_clock::now();
      auto res = mc.fn(state, opts);
      const auto t1 = std::chrono::steady_clock::now();
      const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

      ++st.attempted;
      if (!res) {
        ++st.failures;
        const SolveError e = res.error();
        if (e == SolveError::Timeout) {
          ++st.timeouts;
        } else if (e == SolveError::DatabaseMissing) {
          ++st.database_missing;
        } else if (e == SolveError::Unsolvable) {
          ++st.unsolvable;
        } else if (e == SolveError::Cancelled) {
          ++st.cancelled;
        } else {
          ++st.other_errors;
        }
        if (st.failures_detail.size() < 5u) {
          st.failures_detail.push_back(
              FailureRecord{trial, join_tokens(scramble), error_name(e), ms});
        }
        std::cerr << std::format(
            "[fuzz] {} trial {}/{} FAILED ({}) in {:.1f} ms | scramble: {}\n",
            mc.name, trial + 1, cfg.trials, error_name(e), ms,
            join_tokens(scramble));
        continue;
      }

      const int moves = res->total_moves();
      if (!verify_solution(scramble, *res)) {
        ++st.failures;
        ++st.verify_failures;
        if (st.failures_detail.size() < 5u) {
          st.failures_detail.push_back(FailureRecord{
              trial, join_tokens(scramble),
              std::format("verify-fail moves={}", moves), ms});
        }
        std::cerr << std::format(
            "[fuzz] {} trial {}/{} VERIFY FAILED (moves={}) in {:.1f} ms | scramble: {}\n",
            mc.name, trial + 1, cfg.trials, moves, ms,
            join_tokens(scramble));
        continue;
      }

      ++st.successes;
      st.success_ms.push_back(ms);
      st.success_moves.push_back(moves);
      if (ms > st.slowest_ms) {
        st.slowest_ms = ms;
        st.slowest_moves = moves;
        st.slowest_trial = trial;
        st.slowest_scramble = join_tokens(scramble);
      }
      if (((trial + 1) % 100) == 0) {
        std::cerr << std::format("[fuzz] {} progress {}/{}\n", mc.name,
                                 trial + 1, cfg.trials);
      }
    }
    if (budget_hit) {
      st.incomplete = true;
      std::cerr << std::format(
          "[fuzz] {} stopped early: time budget {:.1f}s exceeded\n", mc.name,
          cfg.time_budget_sec);
    }
    const auto method_t1 = std::chrono::steady_clock::now();
    st.method_wall_sec =
        std::chrono::duration<double>(method_t1 - method_t0).count();
    stats.push_back(std::move(st));
  }

  const auto global_t1 = std::chrono::steady_clock::now();
  const double total_wall =
      std::chrono::duration<double>(global_t1 - global_t0).count();

  // Build the final report once; print to stdout and to the output file so
  // both are byte-identical.
  std::string report;
  report += std::format("rubik-solver fuzz benchmark (SPEC section 10)\n");
  report += std::format("seed_base: {} (per-method seed = seed_base + offset; offsets cfop=0 roux=100000 zz=200000 heise=300000)\n", cfg.seed_base);
  report += std::format("requested_trials_per_method: {}\n", cfg.trials);
  report += std::format("scramble: {} random face moves (18-face pool, no same-face repeat); per-method mt19937\n", cfg.human_depth);
  report += std::format("options: human timeout_ms={} max_depth={} thread_count=1\n", cfg.human_timeout_ms, cfg.human_max_depth);
  report += std::format("verify: refsim solved+scramble+solution==solved; timing excludes scramble+verify, excludes warmup; warmup_ms per method below\n");
  if (cfg.trials < 1000) {
    report += std::format("note: reduced run (SPEC section 10 acceptance needs 1000 trials/method); scale per-trial avg/median/p95 to 1000 to extrapolate full runtime\n");
  }
  if (cfg.time_budget_sec > 0.0) {
    report += std::format("note: time budget {}s was set; incomplete methods (if any) are marked below and must not be read as full-N results\n", cfg.time_budget_sec);
  }
  report += std::format("total_wall_sec: {:.1f}\n", total_wall);
  report += "\n";
  report += "method | trials | successes | success_rate | failures | timeouts | db_missing | verify_fail | avg_moves | min_moves | max_moves | avg_ms | median_ms | p95_ms | max_ms\n";
  report += std::format("{:<8} | {:>6} | {:>9} | {:>6}% | {:>8} | {:>8} | {:>10} | {:>11} | {:>9} | {:>9} | {:>9} | {:>8} | {:>9} | {:>8} | {:>8}\n",
                        "method", "trials", "successes", "rate", "fail", "tout", "dbmiss", "verfail",
                        "avg_moves", "min", "max", "avg_ms", "median_ms", "p95_ms", "max_ms");
  report += std::string(150, '-') + "\n";
  bool all_pass = true;
  for (const auto& st : stats) {
    double rate = 0.0;
    if (st.attempted > 0) {
      rate = 100.0 * static_cast<double>(st.successes) /
             static_cast<double>(st.attempted);
    }
    const double avg_moves = mean_of_int(st.success_moves);
    const double avg_ms = mean_of_double(st.success_ms);
    const double med_ms = median_of(st.success_ms);
    const double p95_ms = percentile_of(st.success_ms, 95.0);
    const double mx_ms = max_double(st.success_ms);
    if (st.skipped_database_missing || st.incomplete || st.attempted == 0 ||
        rate < 99.0) {
      all_pass = false;
    }
    report += std::format("{:<8} | {:>6} | {:>9} | {:>6.2f}% | {:>8} | {:>8} | {:>10} | {:>11} | {:>9.2f} | {:>9} | {:>9} | {:>8.2f} | {:>9.2f} | {:>8.2f} | {:>8.2f}\n",
                          st.name, st.requested, st.successes, rate,
                          st.failures, st.timeouts, st.database_missing,
                          st.verify_failures, avg_moves, min_of(st.success_moves),
                          max_of(st.success_moves), avg_ms, med_ms, p95_ms, mx_ms);
  }
  report += "\nPer-method detail:\n";
  for (const auto& st : stats) {
    report += std::format("- {}: seed={} scramble_depth={} timeout_ms={} max_depth={} warmup_ms={:.1f}\n",
                          st.name, st.seed, st.scramble_depth, st.timeout_ms,
                          st.max_depth, st.warmup_ms);
    if (!st.warmup_error.empty()) {
      report += std::format("  warmup_error: {}\n", st.warmup_error);
    }
    if (st.skipped_database_missing) {
      report += "  status: SKIPPED (DatabaseMissing - tables in tables/ missing; see data dir)\n";
    } else if (st.incomplete) {
      report += "  status: INCOMPLETE (time budget hit; attempted < requested)\n";
    } else {
      report += std::format("  status: completed attempted={} (requested={}) method_wall_sec={:.1f}\n",
                            st.attempted, st.requested, st.method_wall_sec);
    }
    report += std::format("  counts: successes={} failures={} timeouts={} database_missing={} unsolvable={} cancelled={} other={} verify_failures={}\n",
                          st.successes, st.failures, st.timeouts,
                          st.database_missing, st.unsolvable, st.cancelled,
                          st.other_errors, st.verify_failures);
    if (!st.success_ms.empty()) {
      report += std::format("  slowest_success: trial={} ms={:.1f} moves={} scramble=\"{}\"\n",
                            st.slowest_trial, st.slowest_ms, st.slowest_moves,
                            st.slowest_scramble);
    }
    if (!st.failures_detail.empty()) {
      report += "  sample_failures (first 5):\n";
      for (const auto& f : st.failures_detail) {
        report += std::format("    trial={} reason={} ms={:.1f} scramble=\"{}\"\n",
                              f.trial, f.reason, f.ms, f.scramble);
      }
    }
  }

  std::cout << report << std::flush;

  {
    std::ofstream out(cfg.output, std::ios::binary | std::ios::trunc);
    if (out) {
      out << report;
      out.flush();
    } else {
      std::cerr << std::format("Warning: could not write results to {}\n", cfg.output);
    }
  }

  if (!all_pass) {
    std::cerr << std::format("[fuzz] acceptance NOT met (>=99% verified per method)\n");
    return 1;
  }
  std::cerr << std::format("[fuzz] acceptance met (>=99% verified per method)\n");
  return 0;
}
