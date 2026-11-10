#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "core/interface.hpp"

namespace rubik::core {

// -- Concepts for the three callables ----------------------------------------
// Keep these as template parameters (not std::function) so hot DFS loops
// inline and movers can bind their own closure types directly.

template <typename Goal, typename S>
concept GoalFn = requires(const Goal& g, const S& s) {
  { g(s) } -> std::convertible_to<bool>;
};

template <typename Heuristic, typename S>
concept HeuristicFn = requires(const Heuristic& h, const S& s) {
  { h(s) } -> std::convertible_to<int>;
};

template <typename Apply, typename S>
concept ApplyFn = requires(const Apply& a, const S& s, int move) {
  { a(s, move) } -> std::convertible_to<S>;
};

// -- Shared iterative-deepening A* searcher ----------------------------------
// Per-stage differences are passed in, never baked in:
//  - `allowed`           : move-id list for the stage (subclassed sets)
//  - `apply`             : (state, moveId) -> next state (fastApply)
//  - `goal`              : state -> bool (stage predicate)
//  - `heuristic`         : state -> int  (max of the stage's PDB lookups)
//  - stage names / PDB wiring stay with the caller; only the search control
//    (DFS + ordering + bound raise + deadline/token) is shared.
template <typename S, typename Apply, typename Goal, typename Heuristic>
  requires ApplyFn<Apply, S> && GoalFn<Goal, S> && HeuristicFn<Heuristic, S>
class IdaSearcher {
 public:
  // abortKind codes (mirrors the solvers' shared convention).
  static constexpr int kAbortNone = 0;
  static constexpr int kAbortTimeout = 1;
  static constexpr int kAbortCancelled = 2;
  // Upper sentinel for bestExceed_; INT_MAX is overflow-safe for tiny bounds.
  static constexpr int kInf = 1 << 28;

  IdaSearcher(const std::vector<int>* allowed, Apply apply, Goal goal,
              Heuristic heuristic)
      : allowed_(allowed),
        apply_(std::move(apply)),
        goal_(std::move(goal)),
        heuristic_(std::move(heuristic)),
        found_flag_(std::make_shared<std::atomic<bool>>(false)) {}

  // Constructor for parallel search with shared found_flag (for root-split across threads).
  IdaSearcher(const std::vector<int>* allowed, Apply apply, Goal goal,
              Heuristic heuristic, std::shared_ptr<std::atomic<bool>> found_flag)
      : allowed_(allowed),
        apply_(std::move(apply)),
        goal_(std::move(goal)),
        heuristic_(std::move(heuristic)),
        found_flag_(std::move(found_flag)) {}

  // -- Configuration ---------------------------------------------------------
  void setDeadline(std::chrono::steady_clock::time_point deadline) {
    hasDeadline_ = true;
    deadline_ = deadline;
  }
  void clearDeadline() { hasDeadline_ = false; }
  void setStopToken(std::stop_token token) { token_ = std::move(token); }
  void setAllowed(const std::vector<int>* allowed) { allowed_ = allowed; }
  // thread_count <= 1 (or unsupported) keeps the exact current
  // single-threaded path. For >1, only the root (g==0) candidate
  // evaluation is parallelised (heuristic+apply); the DFS order and the
  // subsequent baseline std::sort are unchanged, so the first solution
  // found is identical to single-threaded mode. Parallelism never touches shared
  // mutable searcher state: workers only write disjoint slots of the
  // local candidate array, and `apply_`/`heuristic_` are invoked
  // concurrently as pure reads over immutable PDB/move tables.
  void setThreadCount(int n) { threadCount_ = n; }
  [[nodiscard]] int threadCount() const { return threadCount_; }

  [[nodiscard]] std::uint64_t nodes() const { return nodes_; }
  [[nodiscard]] int abortKind() const { return abortKind_; }
  [[nodiscard]] const std::vector<int>& solution() const { return solution_; }

  void reset() {
    nodes_ = 0;
    abortKind_ = kAbortNone;
    found_ = false;
    bestExceed_ = 0;
    path_.clear();
    solution_.clear();
  }

  // Depth-first branch-and-bound under `bound`. Returns true when a goal
  // path was found (stored in solution_). Goal is checked before heuristic
  // and bound pruning (baseline hand-rolled order), so goal states are
  // accepted without a heuristic evaluation. Candidates that exceed the
  // bound (including dead same-face continuations via huge heuristic)
  // are pruned at the parent and never recursed into; only live
  // (f <= bound) candidates are ordered and expanded.
  [[nodiscard]] bool dfs(const S& state, int g, int bound) {
    if (found_) return true;
    // Early abort: if another thread found a solution at this threshold, stop.
    if (found_flag_ && found_flag_->load(std::memory_order_acquire)) return false;
    if (goal_(state)) {
      solution_ = path_;
      found_ = true;
      if (found_flag_) found_flag_->store(true, std::memory_order_release);
      return true;
    }
    ++nodes_;
    // Poll at the root (so even tiny searches honor an expired deadline or
    // a pre-stopped token) and then every 1024 nodes.
    if (g == 0 || (nodes_ & 0x3FFu) == 0u) {
      if (pollAbort()) return false;
      // Also check the shared found_flag at poll points.
      if (found_flag_ && found_flag_->load(std::memory_order_acquire)) return false;
    }

    const int h = heuristic_(state);
    const int f = g + h;
    if (f > bound) {
      // Track the smallest exceeding f seen this iteration.
      bestExceed_ = (std::min)(bestExceed_ == 0 ? f : bestExceed_, f);
      return false;
    }
    if (allowed_ == nullptr) return false;

    struct Cand {
      int move = 0;
      S state;
      int h = 0;
    };
    constexpr std::size_t kStackCap = 32;
    const std::size_t n = allowed_->size();
    // Heap fallback for unusually wide move sets (>32): preserves the exact
    // pre-stack-array semantics; all production stages use <=18.
    std::vector<Cand> heap;
    std::array<Cand, kStackCap> stackBuf{};
    Cand* cands = nullptr;
    if (n <= kStackCap) {
      cands = stackBuf.data();
      const bool parallelRoot =
          threadCount_ > 1 && g == 0 && n > 1;
      if (!parallelRoot) {
        for (std::size_t i = 0; i < n; ++i) {
          if (found_flag_ && found_flag_->load(std::memory_order_acquire)) return false;
          const int m = (*allowed_)[i];
          Cand& c = cands[i];
          c.move = m;
          c.state = apply_(state, m);
          c.h = heuristic_(c.state);
        }
      } else {
        // Bounded root parallelism: partition candidate indices into
        // contiguous chunks; each worker writes only its chunk, evaluates
        // heuristic/apply over immutable tables. Ordering afterwards is a
        // plain std::sort, so the DFS order and returned solution are
        // identical to the single-threaded path.
        const int nt = (std::min)(threadCount_, static_cast<int>(n));
        std::atomic<bool> should_abort(false);
        auto worker = [&](int t) {
          const std::size_t begin = static_cast<std::size_t>(t) * n /
                                      static_cast<std::size_t>(nt);
          const std::size_t end = static_cast<std::size_t>(t + 1) * n /
                                    static_cast<std::size_t>(nt);
          for (std::size_t i = begin; i < end; ++i) {
            if (should_abort.load(std::memory_order_acquire)) return;
            if (found_flag_ && found_flag_->load(std::memory_order_acquire)) return;
            const int m = (*allowed_)[i];
            Cand& c = cands[i];
            c.move = m;
            c.state = apply_(state, m);
            c.h = heuristic_(c.state);
          }
        };
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(nt - 1));
        for (int t = 1; t < nt; ++t) {
          pool.emplace_back(worker, t);
        }
        worker(0);
        for (auto& th : pool) {
          th.join();
        }
      }
    } else {
      heap.reserve(n);
      for (const int m : *allowed_) {
        S child = apply_(state, m);
        heap.push_back(Cand{m, child, heuristic_(child)});
      }
      cands = heap.data();
    }
    // Parent-side bound pruning (baseline semantics): candidates with
    // f = g+1+h > bound are recorded in bestExceed_ here and excluded from
    // ordering/recursion. Dead continuations (huge heuristic) are covered
    // by the same f test, so they never expand. Compaction preserves
    // allowed-list order among live candidates; std::sort by h (f is
    // monotonic in h) then reproduces the exact baseline unstable sort
    // on the same live-candidate sequence (baseline sorts by (f, h),
    // which is the same comparator relation). No move-index tiebreak is
    // applied: ties follow the baseline std::sort permutation.
    std::size_t m = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const int ff = g + 1 + cands[i].h;
      if (ff > bound) {
        bestExceed_ = (std::min)(bestExceed_ == 0 ? ff : bestExceed_, ff);
        continue;
      }
      if (m != i) cands[m] = cands[i];
      ++m;
    }
    std::sort(cands, cands + m,
              [](const Cand& a, const Cand& b) { return a.h < b.h; });

    for (std::size_t i = 0; i < m; ++i) {
      if (found_ || abortKind_ != kAbortNone) return found_;
      path_.push_back(cands[i].move);
      if (dfs(cands[i].state, g + 1, bound)) return true;
      path_.pop_back();
    }
    return false;
  }

  // Iterative-deepening run. `maxDepth` is the hard cap; `initialBound` < 0
  // starts at heuristic(start). Raises bound to the smallest exceeded f.
  [[nodiscard]] std::expected<std::vector<int>, SolveError> run(
      const S& start, int maxDepth, int initialBound = -1) {
    reset();
    if (allowed_ == nullptr && !goal_(start)) {
      return std::unexpected(SolveError::Unsolvable);
    }
    int bound = (initialBound < 0) ? heuristic_(start) : initialBound;
    while (bound <= maxDepth) {
      bestExceed_ = kInf;
      path_.clear();
      found_flag_->store(false, std::memory_order_release);
      if (dfs(start, 0, bound)) {
        return solution_;
      }
      if (abortKind_ == kAbortTimeout) {
        return std::unexpected(SolveError::Timeout);
      }
      if (abortKind_ == kAbortCancelled) {
        return std::unexpected(SolveError::Cancelled);
      }
      if (bestExceed_ == kInf) {
        return std::unexpected(SolveError::Unsolvable);
      }
      bound = bestExceed_;
    }
    return std::unexpected(SolveError::Unsolvable);
  }

 private:
  [[nodiscard]] bool pollAbort() {
    if (token_.stop_requested()) {
      abortKind_ = kAbortCancelled;
      return true;
    }
    if (hasDeadline_ && std::chrono::steady_clock::now() >= deadline_) {
      abortKind_ = kAbortTimeout;
      return true;
    }
    return false;
  }

  const std::vector<int>* allowed_ = nullptr;
  int threadCount_ = 1;
  Apply apply_;
  Goal goal_;
  Heuristic heuristic_;
  std::shared_ptr<std::atomic<bool>> found_flag_ = std::make_shared<std::atomic<bool>>(false);
  std::vector<int> path_;
  std::vector<int> solution_;
  bool found_ = false;
  int abortKind_ = kAbortNone;
  std::uint64_t nodes_ = 0;
  int bestExceed_ = 0;
  bool hasDeadline_ = false;
  std::chrono::steady_clock::time_point deadline_{};
  std::stop_token token_{};
};

}  // namespace rubik::core