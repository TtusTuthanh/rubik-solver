# CFOP Korf Upgrade — Engineer Report

Branch: perf/korf-cfop
Worktree: H:/wt-cfop
Build dir: H:/wt-cfop/build-cfop
Role: CFOP method engineer (Wave 5 — method adoption)

## Result
All gates met. Committed atomically on perf/korf-cfop (solver.cpp + this report).

- Fuzz (Release, serial, `--methods cfop --trials 1000 --seed-base 20261108`): exit 0
  - success 100.00% (1000/1000), 0 failures, 0 timeouts            [gate ≥99% ✓]
  - median 41.00 ms                                                 [gate ≤111.35 ms ✓]
  - p95 234.03 ms                                                   [gate ≤764.86 ms ✓]
  - max 1768.08 ms; avg 74.49 ms; avg moves 54.16 (min 36 / max 63)
  - total wall 79.8s (down from 368.8s pre-fix)
- ctest (build-cfop): 15/15 passed
- rubik_cfop_admissibility_tests: PASS (6/6) — O(1) index transplant
  behavior-preserving; cross15/pair15 addition to OLL remains admissible
- rubik_cfop_differential_tests: PASS (F-06) — OLL trial-14 timeout fixed;
  optimized move counts ≤ baseline per scramble
- Full ctest suite: 15/15 green

## Perf log (this machine; gates are absolute vs BASELINE.md)
| Change | cfop median | cfop p95 | OLL timeouts | notes |
|--------|-------------|----------|--------------|-------|
| baseline | 118.86 | 891.26 | - | this machine ~12% slower than BASELINE.md |
| pre-OLL-fix (post O(1) transplant) | 116.46 | 844.37 | 3 | OLL pathology: 3 timeouts, max 7109.91 ms |
| OLL fix: re-add cross15+pair15 | 41.00 | 234.03 | 0 | admissible tightening; wall 79.8s |

## Change
`src/solvers/cfop/solver.cpp` — `stageHeuristic(StageKind::Oll)`: re-added `cross15` and the
four `pair15` PDB terms (dropped previously with the reasoning "exactly 0 once F2L is
complete"). That reasoning is wrong for the search: the 15 non-D OLL moves disturb the
D-layer cross and F2L pairs mid-search, so `cross15`/`pair15` are real admissible lower
bounds on the remaining OLL cost. They are 0 at the OLL start state (F2L complete) but
rise as soon as F2L is disturbed, pruning the weak-orientation-only branches that
previously exploded IDA* (was 55.7% of solve time; source of the 3 timeouts).
Each term is a lower bound on a required subgoal; the max of admissible heuristics is
admissible, so per-stage optimality is preserved. Fixes the OLL pathology and the
differential-test trial-14 timeout.

## Gates (absolute, vs BASELINE.md: cfop median ≤111.35 ms, p95 ≤764.86 ms, success ≥99%)
- exit 0: ✓
- success ≥99%: ✓ 100.00%
- median ≤111.35 ms: ✓ 41.00 ms
- p95 ≤764.86 ms: ✓ 234.03 ms
- OLL ≥10% better: ✓ 0 OLL timeouts vs 3; OLL >1s solves eliminated

## Scope
Write set: `src/solvers/cfop/solver.cpp` only (+ this report). No changes to
`src/core/*`, `src/tables/*`, other solvers, `benchmarks/fuzz.cpp`,
`tests/core/reference_sim.hpp`. No new deps beyond GoogleTest. Commit only on
`perf/korf-cfop` (no push). `build-cfop/` is a build artifact (not committed).
