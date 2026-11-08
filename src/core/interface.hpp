#pragma once
#include <expected>
#include <stop_token>
#include <string>
#include <vector>
#include "core/cube.hpp"

namespace rubik {

struct SolveOptions {
  int timeout_ms{30000};
  int max_depth{20};
  int thread_count{4};
  std::stop_token stop_token{};
};

struct StageSolution {
  std::string stage_name;
  std::vector<std::string> moves; // each token counts as 1: "R", "R'", "R2", "M", ...
};

struct Solution {
  std::vector<StageSolution> stages;
  int total_moves() const {
    int n = 0;
    for (auto const& s : stages) n += static_cast<int>(s.moves.size());
    return n;
  }
};

enum class SolveError {
  Unsolvable,
  Timeout,
  Cancelled,
  DatabaseMissing,
  InvalidInput,
  NotImplemented,
};

enum class Method { Cfop, Roux, Zz, Heise, Optimal };

// Registry: method name -> solver. Each solver provides
//   std::expected<Solution, SolveError> solve(const CubeState&, const SolveOptions&)
// in its own namespace (rubik::<method>::solve). Frozen at P1.
using SolverFn = std::expected<Solution, SolveError> (*)(const CubeState&, const SolveOptions&);

} // namespace rubik
