# Roux / ZZ / Heise re-migration spec onto improved shared component

Scope: ONE commit per method, read-only vs baseline until migration lands.
Baseline `9b6e14c` — seed 42, 50 scrambles × 25 moves, 10 s per-run timeout
(`benchmarks/baseline/scrambles.txt`, `.scratch/baseline/results.json`).
Current HEAD state: roux/zz/heise solvers byte-identical to baseline after
reverts `34dbe62` / `3d3b79b` / `72f0362`. Do NOT edit them until the shared
component lands (blocked dependency §5).

## 1. What regressed before (evidence)

| Commit | What happened | Lesson |
|---|---|---|
| `c5262dc` shared `IdaSearcher` | `vector<Cand>` per node + `std::sort` (unstable) by `h` only | Unstable ties + per-node heap alloc |
| `3070060` roux / `9fe2caf` cfop | migrated WITHOUT same-face pruning | Dropped `prevGroup`/`prevFace` entirely → blowup (roux #50-style) |
| `60a9551` cfop fix | `SearchState{prevFace,dead}` + `kPruneHeuristic=1<<25` | Dead-child pruning wrapper is mandatory |
| `7e3d991` roux fix | same wrapper for roux | Roux needed the fix too; reverted anyway |
| `0d17fcf` | `sort`→`stable_sort` | Fixed tie nondeterminism, but median shifts can still flip timeouts |
| `db9644a` zz / `88a99bf` heise | already had wrapper | ZZ failed only on marginal #27; heise failed on tie-shift timeouts (3 deterministic) |

Pre-revert migrated parent hashes for diff reference: roux `3070060`+`7e3d991`
era, zz `db9644a`, heise `88a99bf`, cfop `9fe2caf`+`60a9551` (template, landed).

## 2. Improved component (parallel worker — present uncommitted in working copy)

- `src/core/searcher.hpp` (uncommitted diff vs HEAD): `IdaSearcher` with
  stack candidate array (`std::array<Cand,32>` + heap fallback for >32, no
  per-node `vector`/`new` for production ≤18 moves), deterministic stable
  ordering (`stable_sort` by `h` ascending, ties keep `allowed`-list order),
  no per-node allocation, `setThreadCount(int)` plumbed (parallel root-only
  candidate evaluation; DFS order + solution identical to single-threaded —
  keep parity runs single-threaded). Verify `tests/core/test_searcher.cpp`
  passes before starting §3.
- `src/core/pdb.hpp` unchanged: `pdbLookup` (255→0, OOB→0) + `bfsDb` (dense
  BFS fill, 255 sentinel, 254 cap). PDB BFS + solver wiring live here, not in
  the searcher.
- Per-method code MUST use the pruning wrapper (§3) with `kPruneHeuristic`
  (`1<<25` roux/zz/cfop, `1<<24` heise — keep the per-method constant, do not
  unify) and MUST remap `Unsolvable`→`Timeout` when the stage bound is
  exceeded (cfop probe pattern: depth-40 probe to distinguish Timeout vs
  Unsolvable).

### Why the stack array eliminates the #50-style blowups

Old `searcher.hpp` did `cands.reserve(n); cands.push_back(...)` per DFS node
plus unstable `std::sort`. At roux/heise depth (branching 18 → pruned ~12–15,
millions of nodes), that is millions of heap allocations + allocator
contention + cache misses. Tie order was implementation-defined, so equal-`h`
children were explored in arbitrary order — a single tie flip on scramble #50
(or any deep stage) could double the subtree. The stack array:

- allocates once on the call stack (`Cand {move, SearchState, h}` × 32),
  zero per-node allocation, cache-local, thread-safe by construction;
- stable order matches baseline hand-rolled DFS exactly (insertion order =
  `allowed` order), so timeout identity is preserved;
- bounds the candidate set (max 18 moves < 32), so overflow is impossible
  for all three methods (compile-time assert or `static_assert(18<=32)`).

## 3. Per-method checklist

### 3a. Roux — `src/solvers/roux/solver.cpp`

Stages (exact, do not rename): `first_block`, `second_block`, `cmll`, `lse`
(`stage_names()` returns `{"first_block","second_block","cmll","lse"}`).
Move sets: `faceMoveIndices()` (all 18) for FB/SB/CMLL, `lseMoveIndices()`
(M/U only) for LSE.

- [ ] Include `core/pdb.hpp` + `core/searcher.hpp`; delete local `pdbLookup`.
- [ ] PDB build: `buildCombinedDb`/`buildLse` via `core::bfsDb` (single-start);
      keep hand-rolled multi-source CMLL seeding (4 U-rotation sources) with
      a comment. No PDB content change.
- [ ] `StageKind{FirstBlock,SecondBlock,Cmll,Lse}`; free functions
      `heuristicFor(kind,pdbs,s)` / `stageGoal(kind,s)` (cumulative max:
      FB=`fb`; SB=`fb+sb`; CMLL=`fb+sb+cmll`; LSE=`lse` with
      `centreRotOf`/`cornerRotOf` non-negative guard).
- [ ] Pruning wrapper (copy cfop `60a9551` verbatim):
      `struct SearchState{CubeState cube; int prevFace=-1; bool dead=false;}`;
      `kPruneHeuristic=1<<25`; `apply` returns dead when
      `face==prevFace` (`md.faceGroup[move]`); `goal=!dead && stageGoal`;
      `heur=s.dead?kPruneHeuristic:heuristicFor(...)`.
- [ ] `runStage`: goal-now→`{}`; `heuristic(start)>stageLimit`→`Timeout`
      before search; setDeadline/setStopToken only if set; run from
      `SearchState{start,-1,false}`. Preserve historical Timeout contract.
- [ ] `solve()`: loop `stage_names()` in order; keep per-stage
      `movesAllowed` + predicate post-checks; keep `lseIdx` dispatch for
      `"lse"` only. No stage-name change, no move-set change.

### 3b. ZZ — `src/solvers/zz/solver.cpp`

Stages: `eoline`, `zzf2l_left`, `zzf2l_right`, `coll`, `eppl`.
Move sets: `faceIdx` (18) for Eoline, `zzIdx` (R/L/U, 9) for Left/Right/Coll/Eppl.

- [ ] Same includes/deletions as roux. Prefer `RowView`/`ArrayView`
      allocation-free neighbors for `bfsDb` (already in `db9644a` — reuse).
- [ ] `StageKind{Eoline,Left,Right,Coll,Eppl}`; `heuristicFor` cumulative max
      (Eoline=`eoline`; Left=`+left`; Right=`+right`; Coll=`+coll`;
      Eppl=`+uedge+perm`); `stageGoal` with Eoline conjunct for Left/Right
      post-checks.
- [ ] Identical pruning wrapper (`SearchState.prevFace/dead`,
      `kPruneHeuristic=1<<25`). ZZ has NO opposite-face rule — do not add it.
- [ ] `runStage`: remap shared `Unsolvable`→`Timeout` (stages always reachable
      from valid cubies; failure = bound exceeded). Keep same-face logic.
- [ ] `solve()`: keep `stageAssert` (Left/Right require
      `is_*_solved && is_eoline_solved`); move sets unchanged.

### 3c. Heise — `src/solvers/heise/solver.cpp` (justified duplication exception)

Stages (`stage_names()`): `squares`, `f2l_minus_1`, `corner_edge_pairs`,
`final_corners`. Internal `StageKind{Squares,Mid1,Mid2,Mid3,Orient,BrPair,
Urf,UEdges,Pairs,Final,Oll}` with sub-stage expansion in `solve()` —
keep both levels intact. Move set: `faceIdx` (18) for every stage.

- [ ] Same includes/deletions. `buildSingleDb`/`buildCombinedPermDb` via
      `bfsDb` returning `std::array<uint32_t,18>` (as in `88a99bf`).
- [ ] `stageHeuristic(kind,pdbs,s)` / `stageGoal(kind,s)` — copy baseline
      heuristic tables verbatim (dEdge/dOrient/f2lPair/midEdge/eo/urf/uEdge/
      cornerPair/br/pairPerm/ori5/finalCorner3/oriFull/finalPerm). No PDB
      content change.
- [ ] Pruning wrapper with TWO rules (this is heise-specific — do not
      simplify to roux/zz): same-face skip PLUS opposite-face canonical
      ordering (groups 0↔3,1↔4,2↔5; skip when `group>prevGroup`).
      Implement as `SearchState{cube, prevGroup=-1 root / -2 dead}`,
      `kDeadHeuristic=1<<24`; illegal transition → dead child with huge `h`.
      Dead bound pollution is harmless (real exceed values are the minimum).
- [ ] `runStage`: goal/Timeout/Unsolvable contract as in `88a99bf`
      (copy its `runStage` tail — do not re-derive).
- [ ] `solve()`: keep Mid1–Mid3/Orient sub-loop and BrPair/Oll sub-loop,
      Pairs/Final loops, and all post-checks. No stage-name/move-set change.

### Forbidden in all three (any = automatic revert)

- Stage-name literals, `stage_names()` order, `allowed_moves_for_stage`
  tokens, `faceGroup`/`md.faceGroup` mapping (`i/3`), PDB entry counts,
  BFS seeding, heuristic max-composition, `stageLimit`/timeout plumbing.
- Adding/removing opposite-face pruning except heise; unifying the
  `kPruneHeuristic` constants; touching `src/core/pdb.hpp` BFS semantics.

## 4. Failure guardrails (full-50, seed 42, per-run 10 s)

| Method | Baseline median | Mean moves | Baseline timeouts | PASS threshold (candidate) | WATCH |
|---|---|---|---|---|---|
| roux | 877 ms | 37.1 (37.08) | 1 (scramble #10) | median ≤ 1100 ms, moves ≤ 40, timeouts ≤ 1 | any new timeout ≠ #10 |
| zz | 988 ms | 49.4 (49.42) | 0 | median ≤ 1250 ms, moves ≤ 52, timeouts = 0 | #27 near-timeout (9956 ms) |
| heise | 3522 ms | 75.7 (75.66) | 3 (#17,#26,#31) | median ≤ 4400 ms, moves ≤ 80, timeouts ≤ 3 AND identity ⊆ {17,26,31} | median improvement with new timeout IDs (prior 2127 ms + 6 timeouts) = FAIL |

- Compare against `.scratch/baseline/results.json` (median/p95/mean-moves
  + per-scramble rows). A median win does NOT compensate a new timeout
  (heise precedent).
- cfop is already migrated — re-run it as a regression tripwire with every
  candidate.

## 5. Machine-variance handling (Risk 5) + commit granularity

- Fixed seed (42); count timeouts separately from median/moves; never
  average timeouts into the median.
- Borderline scrambles (zz #27; roux #50; heise #17/#26/#31): report
  median-of-3 (3 runs, fresh process each) — a single marginal timeout is
  not a signal.
- Parity runs single-threaded (`thread_count=1` / `--threads` unset);
  `thread_count>1` results are informational only until the searcher’s
  parallel root-partition is merged and benchmarked.
- Verify goal-identity: same timeout indices at same bounds, not just counts.
- Commit granularity: ONE commit per method (`refactor: migrate roux/zz/heise
  to shared IdaSearcher`), each squashed with its own full-50 results.
  Order: roux → zz → heise (roux is the template for the wrapper; heise last
  because its dual pruning is most fragile).

## 6. Risk table

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Tie-order flip on deep scramble | High | New timeouts (heise) | Stack array + stable order; timeout-identity check |
| Per-node `vector` alloc blowup | High | #50-style blowup | Stack array (max 32), no heap in DFS |
| Dropped same-face pruning | Medium | Massive slowdown | Mandatory `SearchState`+`kPruneHeuristic` checklist |
| Heise opposite-face rule dropped | Medium | Slowdown / wrong moves | Dual-rule wrapper, keep `kDeadHeuristic=1<<24` |
| `Unsolvable` vs `Timeout` contract drift | Medium | Wrong error surfaced | Copy cfop probe / zz remap; unit tests in `tests/core/` |
| Machine variance on borderline scrambles | Medium | False accept/reject | Median-of-3 on #27/#50/#17/#26/#31; single-thread parity |

## 7. Blocked dependency

Do NOT start any of §3 until `src/core/searcher.hpp` stack-array +
deterministic ordering + `setThreadCount` is COMMITTED (currently
uncommitted in the working copy on this branch) and
`tests/core/test_searcher.cpp` passes. This spec assumes that API; adapting
after the fact invalidates the per-method diffs above. cfop's use of it is
the reference wiring.
