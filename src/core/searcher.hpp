#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
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

// -- Redundant-move pruning ---------------------------------------------------

// Policy describing which moves are redundant when they follow a given move.
//
// Korf (1997) uses two lossless rules for the face-turn metric:
//   * two consecutive turns of the same face compose to a single turn, so no
//     optimal solution contains them;
//   * opposite faces commute, so only one of the two consecutive orders need
//     be kept.
// Both rules preserve optimality, so a searcher that skips redundant
// candidates still returns an optimal solution.
//
// The policy is a NON-OWNING, READ-ONLY view over the pruning table's byte
// buffer.  It holds a `shared_ptr` to that buffer, so the buffer stays alive
// for as long as any copy of the policy exists: destroying the table while a
// policy is in use can no longer cause a use-after-free.
//
// LIFETIME AND INDEX-SPACE CONTRACT (also stated at
// `tables::MovePruningTable::policy()`, the producer side):
//   * The table must remain alive, and must not be moved-from, destroyed,
//     reassigned or rebuilt, for as long as any policy derived from it is in
//     use by an `IdaSearcher`.
//   * A policy must be RE-OBTAINED from `policy()` after the table is moved
//     or copied: the copy owns a different buffer object.
//   * A policy obtained from a MOVED-FROM table is INACTIVE
//     (`active() == false`) and silently disables pruning.
//   * The table's move-index space must be the SAME index space as the move
//     ids in the searcher's `allowed_` list; ids outside it are treated as
//     "never redundant", so a mismatch silently disables pruning for those
//     ids rather than failing loudly.
class MovePruningPolicy {
 public:
  MovePruningPolicy() = default;
  MovePruningPolicy(std::shared_ptr<const std::vector<std::uint8_t>> table,
                    int num_moves) noexcept
      : table_(std::move(table)), num_moves_(num_moves) {}

  [[nodiscard]] bool active() const noexcept {
    return static_cast<bool>(table_);
  }

  // Out-of-range move indices are never redundant.
  [[nodiscard]] bool isRedundant(int prev, int curr) const noexcept {
    if (!table_) return false;
    if (prev < 0 || curr < 0) return false;
    if (prev >= num_moves_ || curr >= num_moves_) return false;
    const std::vector<std::uint8_t>& table = *table_;
    const std::size_t base =
        static_cast<std::size_t>(prev) * static_cast<std::size_t>(num_moves_);
    return table[base + static_cast<std::size_t>(curr)] != 0;
  }

 private:
  std::shared_ptr<const std::vector<std::uint8_t>> table_;
  int num_moves_ = 0;
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
  // Optional redundant-move pruning policy. When active, a candidate move
  // that is redundant after the previous move is never generated: `apply_`
  // is not called and `heuristic_` is not evaluated for it. Leaving the
  // policy inactive preserves the previous behaviour exactly (every allowed
  // move is generated at every node).
  //
  // LIFETIME AND INDEX-SPACE CONTRACT (the consumer side; the producer side
  // is `tables::MovePruningTable::policy()`):
  //   * `MovePruningPolicy` is a non-owning, read-only view over the pruning
  //     table's byte buffer. The table must remain alive, and must not be
  //     moved-from, destroyed, reassigned or rebuilt, for as long as the
  //     policy is in use by this searcher.
  //   * A policy must be re-obtained from `policy()` after the table is
  //     moved or copied.
  //   * A policy obtained from a moved-from table is inactive
  //     (`active() == false`) and silently disables pruning.
  //   * The table's move-index space must be the same index space as the
  //     move ids in the searcher's `allowed_` list; ids outside it are
  //     treated as "never redundant", so a mismatch silently disables
  //     pruning for those ids rather than failing loudly.
  //
  // (The policy shares ownership of the buffer, so the buffer itself cannot
  // be freed while the searcher holds the policy -- but the contract above
  // still applies to the TABLE's identity and index space.)
  void setMovePruning(const MovePruningPolicy& policy) noexcept {
    pruning_ = policy;
  }
  [[nodiscard]] bool movePruningActive() const noexcept {
    return pruning_.active();
  }
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
  //
  // INVARIANT: `path_` holds exactly the `g` moves that led to `state`, so
  // `path_.back()` is the previous move on the current path. `run()` clears
  // the path and enters with g == 0; the recursion pushes before descending
  // and pops after the child returns. The invariant is asserted, not merely
  // documented, because the redundant-move rule depends on `path_.back()`
  // being the true previous move.
  [[nodiscard]] bool dfs(const S& state, int g, int bound) {
    assert(path_.size() == static_cast<std::size_t>(g) &&
           "dfs entered with a g that does not match the path depth");
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
    // Redundant-move pruning (Korf 1997). The previous move is the last move
    // on the current path; at the root there is none. When a pruning policy
    // is active, a redundant candidate is never generated: `apply_` is not
    // called and `heuristic_` is not evaluated for it. `num_cands` counts the
    // candidates actually generated, so the parent-side bound pruning below
    // only ever inspects live candidates.
    const int prev_move = path_.empty() ? -1 : path_.back();
    const bool prune = pruning_.active() && prev_move >= 0;
    // Heap fallback for unusually wide move sets (>32): preserves the exact
    // pre-stack-array semantics; all production stages use <=18.
    std::vector<Cand> heap;
    std::array<Cand, kStackCap> stackBuf{};
    Cand* cands = nullptr;
    std::size_t num_cands = 0;
    if (n <= kStackCap) {
      cands = stackBuf.data();
      const bool parallelRoot =
          threadCount_ > 1 && g == 0 && n > 1;
      if (!parallelRoot) {
        for (std::size_t i = 0; i < n; ++i) {
          if (found_flag_ && found_flag_->load(std::memory_order_acquire)) return false;
          const int m = (*allowed_)[i];
          if (prune && pruning_.isRedundant(prev_move, m)) continue;
          Cand& c = cands[num_cands++];
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
        //
        // INVARIANT (asserted, not merely documented): this branch is
        // reachable only at the root (g == 0), where `path_` is empty and
        // therefore `prune` is false. Every allowed move is generated, so
        // the workers' allowed-list-indexed slots `cands[i]` coincide
        // exactly with the serial branch's compacted slots
        // `cands[num_cands++]`, and `num_cands == n`.
        //
        // If this branch were ever extended to g > 0, the two layouts
        // would DIVERGE (the workers write only the surviving entries into
        // allowed-index slots while `num_cands = n` would overstate the
        // count) and the compaction loop below would read UNINITIALISED
        // `Cand` entries. The assertions below make that a hard failure
        // instead of a latent undefined-behaviour bug.
        assert(!prune &&
               "the parallel root branch requires prune == false (g == 0)");
        assert(path_.empty() &&
               "the parallel root branch requires an empty path (g == 0)");
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
        num_cands = n;
        assert(num_cands == n &&
               "the parallel root branch generates every allowed move");
      }
    } else {
      heap.reserve(n);
      for (const int m : *allowed_) {
        if (prune && pruning_.isRedundant(prev_move, m)) continue;
        S child = apply_(state, m);
        heap.push_back(Cand{m, child, heuristic_(child)});
      }
      cands = heap.data();
      num_cands = heap.size();
    }
    // Parent-side bound pruning (baseline semantics): candidates with
    // f = g+1+h > bound are recorded in bestExceed_ here and excluded from
    // ordering/recursion. Compaction preserves allowed-list order among live
    // candidates; std::sort by h (f is monotonic in h) then reproduces the
    // exact baseline unstable sort on the same live-candidate sequence
    // (baseline sorts by (f, h), which is the same comparator relation). No
    // move-index tiebreak is applied: ties follow the baseline std::sort
    // permutation.
    //
    // `num_cands` is the number of candidates actually generated; with
    // redundant-move pruning active it is smaller than `n`, and the skipped
    // moves are absent from this list entirely (they were never applied and
    // never scored).
    std::size_t m = 0;
    for (std::size_t i = 0; i < num_cands; ++i) {
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
  MovePruningPolicy pruning_{};
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