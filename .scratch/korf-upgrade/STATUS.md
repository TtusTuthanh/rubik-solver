# Korf Stage PDB Upgrade Status

## Wave Progress

| Wave | Status | Details |
|------|--------|---------|
| Wave 0: Git Setup | ✅ Complete | Branch `perf/korf-stage-pdb` created, tag `korf-base` created, worktree `../rubik-baseline` added |
| Wave 1: Parallel Discovery | 🔄 In Progress | 4 subagents spawned |
| Wave 2: Foundation | ⏳ Pending | Waits for gui-remover commit |
| Wave 3: Method Agents | ⏳ Pending | Waits for Wave 2 |
| Wave 4: Integration | ⏳ Pending | Waits for Wave 3 |
| Wave 5: Verification & Docs | ⏳ Pending | Waits for Wave 4 |

## Subagent Status (Wave 1)

| Subagent | Status | Deliverable |
|----------|--------|-------------|
| gui-remover | 🔄 Spawned | `.scratch/korf-upgrade/results/gui-removal-report.md` |
| baseline-bench | 🔄 Spawned | `.scratch/korf-upgrade/results/baseline/baseline-report.md` |
| crux-researcher | 🔄 Spawned | `.scratch/korf-upgrade/design/crux-notes.md` |
| method-cartographer | 🔄 Spawned | `.scratch/korf-upgrade/design/method-map.md` |

## Key Metrics (to be filled)

| Method | Baseline Median Time | Baseline P95 Time | New Median Time | New P95 Time | Baseline Nodes | New Nodes | Baseline Moves | New Moves |
|--------|---------------------|-------------------|-----------------|--------------|----------------|-----------|----------------|-----------|
| CFOP   | - | - | - | - | - | - | - | - |
| Roux   | - | - | - | - | - | - | - | - |
| ZZ     | - | - | - | - | - | - | - | - |
| Heise  | - | - | - | - | - | - | - | - |

## Files Deleted / Relocated

### Deleted (GUI-only)
- [ ] `src/visualiser.cpp`
- [ ] `src/visualiser/main_3d.cpp`
- [ ] `src/visualiser/renderer.hpp`
- [ ] `src/visualiser/renderer.cpp`
- [ ] `src/visualiser/facelet.hpp`
- [ ] `src/visualiser/facelet.cpp`
- [ ] `tests/visualiser/test_renderer.cpp`
- [ ] `tests/visualiser/test_facelet.cpp` (or adapted)
- [ ] GUI parts of `CMakeLists.txt` (GLFW, ImGui, OpenGL, visualiser targets)

### Relocated
- [ ] `tests/visualiser/test_integration.cpp` → `tests/integration/test_solver_replay.cpp`
- [ ] Facelet conversion functions → `src/core/facelet_conversion.hpp/.cpp` (if needed by non-GUI code)

## Skills Loaded

### Orchestrator
- [x] using-agent-skills
- [x] planning-and-task-breakdown

### Subagents (max 3 each)
- gui-remover: deprecation-and-migration, incremental-implementation
- baseline-bench: performance-optimization
- crux-researcher: source-driven-development
- method-cartographer: improve-codebase-architecture (read-only)
- foundation-architect: tdd, test-driven-development, incremental-implementation, diagnosing-bugs, code-simplification
- method agents: tdd, test-driven-development, incremental-implementation, debugging-and-error-recovery, code-simplification
- integrator: git-workflow-and-versioning
- fuzz-verifier: performance-optimization
- reviewer: code-review-and-quality
- docs-agent: documentation-and-adrs, domain-docs conventions

## Open Questions / Risks

1. **Facelet conversion relocation**: Need to verify if any non-GUI production code uses `rubik::visualiser::facelets_to_cube` or `cube_to_facelets`. Core has `parse_facelets` and `to_facelets` — are they API-compatible?
2. **Test count delta**: Need to record exact test count before/after GUI removal.
3. **Table size budget**: 1 GB default — need to estimate from crux research.
4. **Build time budget**: 15 min default — need to estimate PDB build times.
5. **Thread tie-break rule**: Need to define deterministic tie-breaking for multi-threaded IDA*.

## Blockers

None currently.

## Next Actions

1. Wait for Wave 1 subagents to complete
2. Review gui-remover report and commit
3. Spawn foundation-architect