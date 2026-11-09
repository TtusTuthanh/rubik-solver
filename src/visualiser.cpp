// rubik-visualiser (P5 Option 1): terminal ANSI step-through viewer.
// No new dependencies; uses std::format + std::cout (std::print fails on MinGW).
// Navigation: n = next stage, p = previous stage, q = quit.
// Cube net layout (facelets order U R F D L B, 9 stickers/face row-major):
//        U
//      L F R B
//        D
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

#if defined(_WIN32) || defined(_WIN64)
#include <conio.h>
#endif

namespace {

using rubik::CubeState;
using rubik::SolveOptions;

std::string ansi_color(char c) {
  switch (c) {
  case 'U': return "\x1b[97m"; // white
  case 'R': return "\x1b[91m"; // red
  case 'F': return "\x1b[92m"; // green
  case 'D': return "\x1b[93m"; // yellow
  case 'L': return "\x1b[95m"; // magenta (orange approx)
  case 'B': return "\x1b[94m"; // blue
  default: return "\x1b[0m";
  }
}

void print_net_clean(const std::string& f) {
  auto st = [&](int face, int pos) -> std::string {
    char c = f[static_cast<std::size_t>(face * 9 + pos)];
    return ansi_color(c) + std::string(1, c) + "\x1b[0m";
  };
  // U (3 rows, offset 6 spaces)
  for (int r = 0; r < 3; ++r) {
    std::cout << "      " << st(0, r * 3 + 0) << " " << st(0, r * 3 + 1) << " " << st(0, r * 3 + 2) << "\n";
  }
  // L F R B in one line per row
  for (int r = 0; r < 3; ++r) {
    std::cout << st(4, r * 3 + 0) << " " << st(4, r * 3 + 1) << " " << st(4, r * 3 + 2)
              << " " << st(2, r * 3 + 0) << " " << st(2, r * 3 + 1) << " " << st(2, r * 3 + 2)
              << " " << st(1, r * 3 + 0) << " " << st(1, r * 3 + 1) << " " << st(1, r * 3 + 2)
              << " " << st(5, r * 3 + 0) << " " << st(5, r * 3 + 1) << " " << st(5, r * 3 + 2) << "\n";
  }
  // D
  for (int r = 0; r < 3; ++r) {
    std::cout << "      " << st(3, r * 3 + 0) << " " << st(3, r * 3 + 1) << " " << st(3, r * 3 + 2) << "\n";
  }
}

bool stage_predicate(const std::string& method, const std::string& stage, const CubeState& s) {
  if (method == "cfop") {
    if (stage == "cross") return rubik::cfop::is_cross_solved(s);
    if (stage == "f2l_1") return rubik::cfop::is_f2l_pair_solved(s, 0);
    if (stage == "f2l_2") return rubik::cfop::is_f2l_pair_solved(s, 1);
    if (stage == "f2l_3") return rubik::cfop::is_f2l_pair_solved(s, 2);
    if (stage == "f2l_4") return rubik::cfop::is_f2l_pair_solved(s, 3);
    if (stage == "oll") return rubik::cfop::is_oll_solved(s);
    if (stage == "pll") return rubik::cfop::is_pll_solved(s);
  } else if (method == "roux") {
    if (stage == "first_block") return rubik::roux::is_first_block_solved(s);
    if (stage == "second_block") return rubik::roux::is_second_block_solved(s);
    if (stage == "cmll") return rubik::roux::is_cmll_solved(s);
    if (stage == "lse") return rubik::roux::is_lse_solved(s);
  } else if (method == "zz") {
    if (stage == "eoline") return rubik::zz::is_eoline_solved(s);
    if (stage == "zzf2l_left") return rubik::zz::is_zzf2l_left_solved(s);
    if (stage == "zzf2l_right") return rubik::zz::is_zzf2l_right_solved(s);
    if (stage == "coll") return rubik::zz::is_coll_solved(s);
    if (stage == "eppl") return rubik::zz::is_eppl_solved(s);
  } else if (method == "heise") {
    if (stage == "squares") return rubik::heise::is_squares_solved(s);
    if (stage == "f2l_minus_1") return rubik::heise::is_f2l_minus_1_solved(s);
    if (stage == "corner_edge_pairs") return rubik::heise::is_corner_edge_pairs_solved(s);
    if (stage == "final_corners") return rubik::heise::is_final_corners_solved(s);
  } else if (method == "twophase") {
    if (stage == "phase1") return rubik::twophase::is_phase1_solved(s);
    if (stage == "phase2") return rubik::twophase::is_phase2_solved(s);
  }
  return false;
}

void print_help() {
  std::cout << "Usage: rubik-visualiser --method <cfop|roux|zz|heise|twophase> [--scramble \"R U\"] [--facelets <54>] [--random-depth N] [--seed N] [--data-dir DIR] [--tables-dir DIR] [--timeout-ms N] [--auto]\n";
  std::cout << "  --auto: print all stages without waiting for keys (for tests/scripts).\n";
  std::cout << "Keys: n = next stage, p = previous stage, q = quit.\n";
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
  bool auto_mode = false;
};

int read_key() {
#if defined(_WIN32) || defined(_WIN64)
  return _getch();
#else
  return std::cin.get();
#endif
}

} // namespace

int main(int argc, char* argv[]) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    auto val = [&](std::string_view /*flag*/) -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
    if (a == "--method") args.method = val(a);
    else if (a == "--scramble") args.scramble = val(a);
    else if (a == "--facelets") args.facelets = val(a);
    else if (a == "--random-depth") { args.random_depth = std::stoi(val(a)); args.has_random_depth = true; }
    else if (a == "--seed") args.seed = std::stoi(val(a));
    else if (a == "--data-dir") args.data_dir = val(a);
    else if (a == "--tables-dir") args.tables_dir = val(a);
    else if (a == "--timeout-ms") args.timeout_ms = std::stoi(val(a));
    else if (a == "--auto") args.auto_mode = true;
    else if (a == "--help" || a == "-h") { print_help(); return 0; }
    else { std::cerr << std::format("unknown argument: {}\n", a); print_help(); return 2; }
  }
  if (args.method.empty()) { std::cerr << "missing --method\n"; print_help(); return 2; }

  using rubik::SolverFn;
  std::map<std::string, SolverFn> registry{
    {"cfop", &rubik::cfop::solve}, {"roux", &rubik::roux::solve}, {"zz", &rubik::zz::solve},
    {"heise", &rubik::heise::solve},
    {"twophase", &rubik::twophase::solve},
  };
  auto it = registry.find(args.method);
  if (it == registry.end()) { std::cerr << "unknown method\n"; return 2; }
  (void)args.data_dir;  // legacy flag retained for compatibility
  if (args.tables_dir.empty()) {
    args.tables_dir = rubik::tables::default_tables_dir().string();
  }
  rubik::tables::set_tables_dir(args.tables_dir);

  CubeState state = CubeState::solved();
  std::vector<std::string> scramble_tokens;
  if (!args.facelets.empty()) {
    auto det = rubik::validate_facelets_detailed(args.facelets);
    if (!det) { std::cerr << std::format("invalid facelets: {}\n", det.error().message); return 2; }
    auto p = rubik::parse_facelets(args.facelets);
    state = p.value();
  } else if (!args.scramble.empty()) {
    auto mv = rubik::parse_moves(args.scramble);
    if (!mv) { std::cerr << std::format("invalid scramble: {}\n", mv.error()); return 2; }
    for (auto const& m : mv.value()) scramble_tokens.push_back(m.token);
    state = rubik::apply(CubeState::solved(), mv.value());
  } else if (args.has_random_depth) {
    auto faces = rubik::all_face_moves();
    std::mt19937 rng(static_cast<unsigned>(args.seed));
    std::uniform_int_distribution<std::size_t> d(0, faces.size() - 1);
    std::vector<rubik::Move> mv;
    for (int i = 0; i < args.random_depth; ++i) mv.push_back(faces[d(rng)]);
    for (auto const& m : mv) scramble_tokens.push_back(m.token);
    state = rubik::apply(CubeState::solved(), mv);
  }

  SolveOptions opts;
  opts.timeout_ms = args.timeout_ms;
  opts.max_depth = 30;
  opts.thread_count = 4;
  auto t0 = std::chrono::steady_clock::now();
  auto result = it->second(state, opts);
  auto t1 = std::chrono::steady_clock::now();
  if (!result) {
    std::cout << std::format("solve error: {}\n", static_cast<int>(result.error()));
    return 1;
  }
  auto const& sol = result.value();
  std::cout << std::format("method: {}  stages: {}  total_moves: {}\n", args.method, sol.stages.size(), sol.total_moves());
  std::cout << std::format("elapsed_ms: {}\n", std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

  // cumulative states after each stage (index -1 = initial scramble state)
  std::vector<CubeState> states;
  states.push_back(state);
  CubeState cur = state;
  for (auto const& st : sol.stages) {
    std::vector<rubik::Move> mv;
    for (auto const& tok : st.moves) mv.push_back(rubik::Move{tok});
    cur = rubik::apply(cur, mv);
    states.push_back(cur);
  }

  int idx = 0; // 0 = initial, 1..N = after stage k
  while (true) {
    CubeState shown = states[static_cast<std::size_t>(idx)];
    std::string facelets = rubik::to_facelets(shown);
    std::cout << "---- stage " << idx << " / " << sol.stages.size() << " ----\n";
    if (idx == 0) {
      std::cout << "state: scramble (initial)\n";
    } else {
      auto const& st = sol.stages[static_cast<std::size_t>(idx - 1)];
      std::cout << std::format("after stage: {} ({} moves: ", st.stage_name, st.moves.size());
      for (std::size_t i = 0; i < st.moves.size(); ++i) std::cout << (i ? " " : "") << st.moves[i];
      std::cout << ")\n";
      bool pred = stage_predicate(args.method, st.stage_name, shown);
      std::cout << std::format("predicate [{}]: {}\n", st.stage_name, pred ? "yes" : "no");
    }
    print_net_clean(facelets);
    std::cout << std::format("facelets: {}\n", facelets);
    // refsim cross-check: scramble + stages[:idx] == this state
    {
      std::string s = refsim::solved_facelets();
      if (!args.facelets.empty()) s = args.facelets;
      else s = refsim::apply_sequence(s, scramble_tokens);
      std::vector<std::string> up_to;
      for (int k = 1; k <= idx; ++k) for (auto const& m : sol.stages[static_cast<std::size_t>(k - 1)].moves) up_to.push_back(m);
      s = refsim::apply_sequence(s, up_to);
      std::cout << std::format("refsim matches state: {}\n", (s == facelets) ? "yes" : "no");
    }
    if (idx == static_cast<int>(sol.stages.size())) {
      std::cout << std::format("solved: {}  verified (core is_solved): {}\n", rubik::is_solved(shown) ? "yes" : "no", rubik::is_solved(shown) ? "yes" : "no");
    }
    if (args.auto_mode) {
      if (idx >= static_cast<int>(sol.stages.size())) break;
      ++idx;
      continue;
    }
    std::cout << "press n=next p=prev q=quit\n";
    int k = read_key();
    if (k == 'q' || k == 'Q') break;
    if (k == 'n' || k == 'N') { if (idx < static_cast<int>(sol.stages.size())) ++idx; }
    else if (k == 'p' || k == 'P') { if (idx > 0) --idx; }
  }
  return 0;
}
