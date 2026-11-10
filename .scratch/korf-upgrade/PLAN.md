# Korf Stage PDB Upgrade Plan

## Overview
This plan orchestrates the implementation of Korf-style pattern database IDA* for the four human-method solvers (CFOP, Roux, ZZ, Heise) while removing the visualiser/GUI entirely.

## Wave 0: Git Setup ✓
- [x] Create branch `perf/korf-stage-pdb`
- [x] Tag `korf-base`
- [x] Add worktree `../rubik-baseline` at `korf-base`

## Wave 1: Parallel Discovery (4 subagents)

### 1.1 gui-remover (Writer)
**Objective:** Remove all visualiser/GUI code, relocate facelet conversion and solver-replay tests per TRAP 1/2.
**Exclusive write set:** GUI files + GUI parts of CMake/docs
**Read-only set:** All source for reference scan
**Skills:** deprecation-and-migration, incremental-implementation
**Deliverable:** `.scratch/korf-upgrade/results/gui-removal-report.md`
**Acceptance:**
- [ ] `grep -rniE "visualis|renderer|glfw|opengl|AnimatedCube|OrbitCamera" src tests benchmarks CMakeLists.txt docs AGENTS.md` returns nothing (except changelog)
- [ ] Facelet conversion (`facelets_to_cube`, `cube_to_facelets`) relocated to `src/core/` if used by non-GUI code; otherwise deleted
- [ ] Solver-replay tests (`tests/visualiser/test_integration.cpp`) relocated to `tests/integration/` and pass
- [ ] Fresh configure+build succeeds WITHOUT GL/GLFW installed
- [ ] `ctest` passes; test count before/after recorded with diff explained
- [ ] Commit on `perf/korf-stage-pdb`

**Stop conditions:** Any non-GUI code broken; reference scan reveals unknown dependency.

### 1.2 baseline-bench (Read-only)
**Objective:** Establish performance baseline in `../rubik-baseline`.
**Read-only set:** Entire repo (works in worktree)
**Skills:** performance-optimization
**Deliverable:** `.scratch/korf-upgrade/results/baseline/baseline-report.md` + raw data files
**Acceptance:**
- [ ] Build `../rubik-baseline`, run `ctest`
- [ ] Run `benchmarks/fuzz.cpp` with fixed `seed_base`, 1000 trials per method (CFOP, Roux, ZZ, Heise)
- [ ] Save per method: median/p95 time, nodes expanded, per-stage and total move counts, failures, peak RSS, table-load time
- [ ] Profile hot spots per method (callgrind/perf/gprof output)
- [ ] No modifications to source

**Stop conditions:** Build fails; benchmark crashes; inconsistent results across runs.

### 1.3 crux-researcher (Read-only)
**Objective:** Document crux techniques and data layouts without copying source.
**Read-only set:** Web (crux repo)
**Skills:** source-driven-development
**Deliverable:** `.scratch/korf-upgrade/design/crux-notes.md`
**Acceptance:**
- [ ] Fetch crux README and relevant source files via web
- [ ] Summarize in own words: coordinate move tables (CP/CO/EO/INV_EP) for O(1) index updates; heuristic = max(corner PDB, three 6-edge PDBs); root moves distributed across threads with early abort; redundant-move pruning
- [ ] Document table sizes, file formats, build-time estimates
- [ ] No source code copied (MIT/BSD/Apache only if license present; crux shows none → re-implement from paper)

**Stop conditions:** Cannot access repo; license found that prohibits re-implementation.

### 1.4 method-cartographer (Read-only)
**Objective:** Map each method's stages, goals, allowed moves, current heuristic, constrained pieces.
**Read-only set:** `src/solvers/{cfop,roux,zz,heise}/**`, `docs/methods/heise.md`
**Skills:** improve-codebase-architecture (read-only)
**Deliverable:** `.scratch/korf-upgrade/design/method-map.md`
**Acceptance:**
- [ ] Per method (CFOP, Roux, ZZ, Heise): list stages, goal predicate, allowed move set, current heuristic function, pieces constrained by stage
- [ ] Identify which pieces are disjoint per stage (for sum vs max combination)
- [ ] Note current PDB build functions (`buildPdbs`, `buildCornerDb`, etc.)

**Stop conditions:** Ambiguous stage definitions; missing allowed-move functions.

## Wave 2: Foundation Architect (1 writer, after gui-remover commits)

### 2.1 foundation-architect (Writer)
**Objective:** Design and implement shared infrastructure for stage PDBs.
**Exclusive write set:** `src/core/searcher.hpp`, `src/tables/**`, `tests/core/**` (for new infra tests)
**Read-only set:** All solver code, crux-notes, method-map
**Skills:** tdd, test-driven-development, incremental-implementation, diagnosing-bugs, code-simplification
**Deliverable:** `.scratch/korf-upgrade/design/DESIGN.md` + committed code
**Acceptance:**
- [ ] `DESIGN.md` with per-stage admissibility argument, table sizes, build-time budget
- [ ] Generic stage-PDB builder (backward BFS from stage goal, stage move set, project metric)
- [ ] Max-combinator heuristic (sum only for provably disjoint pattern sets)
- [ ] Move-pruning table (redundant/opposite move elimination per stage metric)
- [ ] Optional root-split parallelism in `IdaSearcher` with early abort
- [ ] Extension point: solvers register stage configs via a trait/struct, never edit shared files
- [ ] All new tables have sampled admissibility tests + entries in `tests/tables/hashes.txt`
- [ ] `ctest` passes; twophase unchanged

**Stop conditions:** Admissibility proof gap; table size > 1 GB; build step > 15 min.

## Wave 3: Method Agents (4 parallel, each in own worktree)

Each agent works in `../wt-<method>` on branch `perf/korf-<method>` with own build dir.

### 3.1 cfop-agent (Writer)
**Exclusive write set:** `src/solvers/cfop/**`, `tests/cfop/**`
**Shared requests:** `.scratch/korf-upgrade/requests/cfop.md`

### 3.2 roux-agent (Writer)
**Exclusive write set:** `src/solvers/roux/**`, `tests/roux/**`
**Shared requests:** `.scratch/korf-upgrade/requests/roux.md`

### 3.3 zz-agent (Writer)
**Exclusive write set:** `src/solvers/zz/**`, `tests/zz/**`
**Shared requests:** `.scratch/korf-upgrade/requests/zz.md`

### 3.4 heise-agent (Writer)
**Exclusive write set:** `src/solvers/heise/**`, `tests/heise/**`
**Shared requests:** `.scratch/korf-upgrade/requests/heise.md`

**Common for all:**
- Failing admissibility test first (h <= true stage distance)
- Implement stage PDBs using shared infra extension point
- Measure against baseline; keep only changes that win (median AND p95 <= baseline)
- Per-stage and total move counts <= baseline for every scramble
- 0 failures, 0 new timeouts, 100% Refsim-verified

## Wave 4: Integration (1 writer)

### 4.1 integrator (Writer)
**Objective:** Merge four method branches into `perf/korf-stage-pdb`.
**Exclusive write set:** Merge conflicts (CMake, test registration, hashes.txt only)
**Skills:** git-workflow-and-versioning
**Acceptance:**
- [ ] Sequential merge of four branches
- [ ] Resolve conflicts only in CMake/test registration/hashes
- [ ] Full `ctest` passes
- [ ] No changes to twophase or interface

## Wave 5: Verification & Documentation (3 parallel + fixer)

### 5.1 fuzz-verifier (Read-only)
**Objective:** Full fuzz run, compare against baseline.
**Read-only set:** Entire repo (on `perf/korf-stage-pdb`)
**Skills:** performance-optimization
**Acceptance:**
- [ ] Same seed and trial count as baseline
- [ ] Per method: median AND p95 time vs baseline; any >5% regression → revert
- [ ] 0 failures, 0 new timeouts, 100% Refsim-verified
- [ ] Per-stage and total move counts <= baseline for every scramble
- [ ] Thread-equivalence checks (deterministic, length-equal for threads=1 vs N)

### 5.2 reviewer (Read-only, fresh context)
**Objective:** Review full diff for correctness.
**Read-only set:** Full diff `korf-base..perf/korf-stage-pdb`
**Skills:** code-review-and-quality
**Deliverable:** `.scratch/korf-upgrade/results/review-findings.md`
**Acceptance:**
- [ ] Findings by severity: admissibility proofs, data races, UB, interface drift, test weakening

### 5.3 docs-agent (Writer)
**Objective:** Update docs, AGENTS.md, GLOSSARY.
**Exclusive write set:** `docs/**`, `AGENTS.md`, `GLOSSARY.md`
**Skills:** documentation-and-adrs, domain-docs conventions
**Acceptance:**
- [ ] All visualiser mentions removed
- [ ] Stage PDBs and new table specs documented
- [ ] One ADR recorded
- [ ] Benchmark results recorded

### 5.4 fixer (Writer, max 2 rounds)
**Objective:** Fix blocking reviewer findings.
**Exclusive write set:** As needed per finding
**Stop:** After 2 rounds or no blocking findings.

### 5.5 Final fuzz-verifier re-run
**Objective:** Confirm all fixes green.

## Assumptions Recorded
1. 5% regression threshold is acceptable default (report if adjusted)
2. Total new table size budget: 1 GB default
3. Build step budget: 15 min default
4. Facelet conversion in visualiser is only used by GUI and tests (core has its own)
5. crux repo has no license → re-implement from Korf 1997 paper
6. Extension point pattern: solver registers stage config struct, shared infra uses it
7. Max-combinator heuristic: sum only for disjoint patterns, otherwise max

## Stop and Ask Items
- Deleting anything outside proven GUI set
- Adding third-party dependencies
- Changing public interface (`Solution`, `StageSolution`, `SolveOptions`, `solve()` signatures)
- Total new table size > 1 GB or build step > 15 min
- Modifying `reference_sim.hpp`
- Disabling/skipping/loosening any test or timeout
- Any push, force-push, history rewrite, or `git clean -fdx` / `reset --hard` on shared tree