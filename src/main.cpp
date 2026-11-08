// rubik-solver CLI (P4). See docs/SPEC.md Section 9.
// Uses std::format + std::cout only (std::print fails to link on MinGW).
#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"
#include "core/moves.hpp"
#include "core/validation.hpp"
#include "solvers/cfop/solver.hpp"
#include "solvers/heise/solver.hpp"
#include "solvers/roux/solver.hpp"
#include "solvers/twophase/solver.hpp"
#include "solvers/zz/solver.hpp"
#include "core/reference_sim.hpp"
#include "tables/tables_loader.hpp"

namespace {

using rubik::SolveError;
using rubik::SolveOptions;

void print_help() {
  std::cout << "Usage: rubik-solver --method <cfop|roux|zz|heise|twophase> [--scramble \"R U R' ...\"] [--facelets <54>] [--random-depth N] [--seed N] [--data-dir DIR] [--tables-dir DIR] [--timeout-ms N]\n";
}

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

struct Args {
  std::string method;
  std::string scramble;
  std::string facelets;
  bool has_random_depth = false;
  int random_depth = 25;
  int seed = 0;
  std::string data_dir;
  std::string tables_dir;
  int timeout_ms = 30000;
};

bool parse_args(int argc, char* argv[], Args& args, std::string& err) {
  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    auto need_value = [&](std::string_view flag) -> std::string {
      if (i + 1 >= argc) { err = std::format("missing value for {}", flag); return {}; }
      return argv[++i];
    };
    if (a == "--method") { args.method = need_value(a); }
    else if (a == "--scramble") { args.scramble = need_value(a); }
    else if (a == "--facelets") { args.facelets = need_value(a); }
    else if (a == "--random-depth") { args.random_depth = std::stoi(need_value(a)); args.has_random_depth = true; }
    else if (a == "--seed") { args.seed = std::stoi(need_value(a)); }
    else if (a == "--data-dir") { args.data_dir = need_value(a); }
    else if (a == "--tables-dir") { args.tables_dir = need_value(a); }
    else if (a == "--timeout-ms") { args.timeout_ms = std::stoi(need_value(a)); }
    else if (a == "--help" || a == "-h") { print_help(); std::exit(0); }
    else if (!err.empty()) { /* keep first error */ }
    else { err = std::format("unknown argument: {}", a); return false; }
  }
  if (args.method.empty()) { err = "missing --method"; return false; }
  return err.empty();
}

} // namespace

int main(int argc, char* argv[]) {
  Args args;
  std::string err;
  if (!parse_args(argc, argv, args, err)) {
    std::cerr << std::format("Error: {}\n", err);
    print_help();
    return 2;
  }

  // Method registry
  using rubik::SolverFn;
  std::map<std::string, SolverFn> registry{
    {"cfop", &rubik::cfop::solve},
    {"roux", &rubik::roux::solve},
    {"zz", &rubik::zz::solve},
    {"heise", &rubik::heise::solve},
    {"twophase", &rubik::twophase::solve},
  };
  auto it = registry.find(args.method);
  if (it == registry.end()) {
    std::cerr << std::format("Error: unknown method '{}' (choose cfop|roux|zz|heise|twophase)\n", args.method);
    return 2;
  }
  // Forward --tables-dir to tables set dir (twophase).
  // Default tables dir is .\\tables via default_tables_dir().
  (void)args.data_dir;  // legacy flag retained for compatibility
  if (args.tables_dir.empty()) {
    args.tables_dir = rubik::tables::default_tables_dir().string();
  }
  rubik::tables::set_tables_dir(args.tables_dir);

  // Build starting state: facelets > scramble > random > solved
  std::vector<std::string> scramble_tokens;
  rubik::CubeState state = rubik::CubeState::solved();
  std::string scramble_display;

  if (!args.facelets.empty()) {
    auto det = rubik::validate_facelets_detailed(args.facelets);
    if (!det) {
      std::cerr << std::format("Error: invalid facelets: {}\n", det.error().message);
      return 2;
    }
    auto parsed = rubik::parse_facelets(args.facelets);
    if (!parsed) {
      std::cerr << std::format("Error: invalid facelets: {}\n", parsed.error());
      return 2;
    }
    state = parsed.value();
    scramble_display = "(facelets input)";
  } else {
    if (!args.scramble.empty()) {
      auto moves = rubik::parse_moves(args.scramble);
      if (!moves) {
        std::cerr << std::format("Error: invalid scramble: {}\n", moves.error());
        return 2;
      }
      for (auto const& m : moves.value()) scramble_tokens.push_back(m.token);
      state = rubik::apply(rubik::CubeState::solved(), moves.value());
    } else if (args.has_random_depth) {
      auto faces = rubik::all_face_moves();
      std::mt19937 rng(static_cast<unsigned>(args.seed));
      std::uniform_int_distribution<std::size_t> dist(0, faces.size() - 1);
      std::vector<rubik::Move> moves;
      for (int i = 0; i < args.random_depth; ++i) moves.push_back(faces[dist(rng)]);
      for (auto const& m : moves) scramble_tokens.push_back(m.token);
      state = rubik::apply(rubik::CubeState::solved(), moves);
    }
    if (scramble_tokens.empty()) {
      scramble_display = "(solved)";
    } else {
      scramble_display = rubik::format_moves([&] {
        std::vector<rubik::Move> ms;
        for (auto const& t : scramble_tokens) ms.push_back(rubik::Move{t});
        return ms;
      }());
      if (scramble_tokens.empty()) scramble_display = "(solved)";
    }
  }
  if (scramble_tokens.empty() && args.facelets.empty() && args.scramble.empty() && !args.has_random_depth) {
    scramble_display = "(solved)";
  }

  std::cout << std::format("method: {}\n", args.method);
  std::cout << std::format("scramble: {}\n", scramble_display);
  std::cout << std::format("facelets: {}\n", rubik::to_facelets(state));

  SolveOptions opts;
  opts.timeout_ms = args.timeout_ms;
  opts.max_depth = 30;
  opts.thread_count = 4;

  auto t0 = std::chrono::steady_clock::now();
  auto result = it->second(state, opts);
  auto t1 = std::chrono::steady_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

  if (!result) {
    std::cout << std::format("error: {}\n", error_name(result.error()));
    std::cout << std::format("elapsed_ms: {}\n", ms);
    return 1;
  }
  auto const& sol = result.value();
  std::cout << "solution:\n";
  std::vector<std::string> all_moves;
  for (auto const& st : sol.stages) {
    std::string line;
    for (std::size_t i = 0; i < st.moves.size(); ++i) {
      if (i) line += " ";
      line += st.moves[i];
      all_moves.push_back(st.moves[i]);
    }
    std::cout << std::format("  {}: {}\n", st.stage_name, line);
  }
  std::cout << std::format("total_moves: {}\n", sol.total_moves());
  std::cout << std::format("elapsed_ms: {}\n", ms);

  // verified via the independent reference simulator: solved + scramble + solution == solved
  bool verified = false;
  {
    std::string s = refsim::solved_facelets();
    std::vector<std::string> prefix;
    if (!scramble_tokens.empty()) prefix = scramble_tokens;
    s = refsim::apply_sequence(s, prefix);
    // If starting from facelets, apply solution directly to that facelet string.
    if (!args.facelets.empty()) s = args.facelets;
    s = refsim::apply_sequence(s, all_moves);
    verified = (s == refsim::solved_facelets());
  }
  std::cout << std::format("verified: {}\n", verified ? "yes" : "no");
  return verified ? 0 : 1;
}
