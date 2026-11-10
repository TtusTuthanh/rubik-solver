# Graph Report - rubik-solver  (2026-11-09)

## Corpus Check
- 85 files · ~182,959 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 1544 nodes · 3866 edges · 65 communities (54 shown, 9 thin omitted)
- Extraction: 92% EXTRACTED · 8% INFERRED · 0% AMBIGUOUS · INFERRED: 317 edges (avg confidence: 0.84)
- Token cost: 50,000 input · 5,400 output

## Community Hubs (Navigation)
- Tables Loading Discovery
- ZZ Solver Pruning
- Heise Solver Pruning
- CFOP Solver Pruning
- Tables Adapter Heuristics
- Facelet Conversion Validation
- Roux Solver Pruning
- Searcher Validation Tests
- Solver Theory Documentation
- IDA Searcher Core
- Fast Apply Interface
- Renderer Interaction Tests
- ZZ Solver Tests
- Benchmark Statistics Tracking
- Heise Solver Tests
- Roux Solver Tests
- Architecture Documentation History
- Animated Cube Replay
- Renderer Window Management
- CFOP Solver Tests
- Move Parsing Formatting
- Renderer GL Drawing
- 3D Math Matrices
- Fuzz Benchmark Harness
- Integration Replay Tests
- Core Interface Headers
- Facelet Validation Tests
- Orbit Camera Picking
- Cube Moves Tests
- Cube Rotation Basis
- Tables Discovery Tests
- Cube State Operations
- Facelet Validation Core
- CLI Args Parser
- Layer Turn Animation
- Reference Simulator
- Validation Unit Tests
- Symmetry Table Layouts
- Phase Two Layouts
- Benchmark Args Parsing
- Sticker Quad Struct
- Benchmark Config
- Cubie Move Data
- Solver Method Config
- Solve Options
- Fuzz Benchmark Documentation
- ANSI Terminal Visualiser
- Heuristic Tables Documentation
- Glossary Domain Documentation
- Issue Tracker Documentation
- Fuzz Failure Records
- Solution Stage Definitions
- Table File Spec
- Tables Benchmark Documentation
- Pattern Database Tables
- Review Delivery Records
- Browser Check Records
- Finalize Review Records
- CFOP Method
- Heise Method
- Roux Method
- ZZ Method
- Ignored Build Artifacts

## God Nodes (most connected - your core abstractions)
1. `CubeState` - 181 edges
2. `TEST()` - 68 edges
3. `solved` - 45 edges
4. `TableLoader` - 41 edges
5. `Renderer` - 38 edges
6. `AnimatedCube` - 36 edges
7. `IdaSearcher` - 35 edges
8. `MethodStats` - 31 edges
9. `Solution` - 31 edges
10. `MoveData` - 31 edges

## Surprising Connections (you probably didn't know these)
- `TEST()` --calls--> `camera_`  [INFERRED]
  tests/visualiser/test_renderer.cpp → src/visualiser/renderer.hpp
- `Reference simulator tests only` --semantically_similar_to--> `Independent reference simulator`  [INFERRED] [semantically similar]
  docs/SPEC.md → README.md
- `Issue tracker agent skill` --semantically_similar_to--> `Local Markdown issue tracker`  [INFERRED] [semantically similar]
  AGENTS.md → docs/agents/issue-tracker.md
- `Tables benchmark WITH vs WITHOUT tables, seed 888002 count 500 depth uniform 1..11` --semantically_similar_to--> `Tables benchmark optimal SAME scrambles seed 888002 count 500 depth uniform 1..11`  [INFERRED] [semantically similar]
  benchmarks/tables_bench.txt → tests/tables/bench.txt
- `Stack-array IdaSearcher (array Cand 32, stable_sort, no per-node allocation)` --conceptually_related_to--> `IdaSearcher IDA* plus PDB`  [INFERRED]
  .scratch/migration/roux_zz_heise_spec.md → .archify/architecture-rubik-solver-20261108-230835/candidate.json

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Five solver methods cfop roux zz heise twophase** — readme_cfop, readme_roux, readme_zz, readme_heise, readme_twophase [EXTRACTED 1.00]
- **Heise canonical four stages** — docs_methods_heise_squares_stage, docs_methods_heise_f2l_1_stage, docs_methods_heise_corner_edge_pairs_stage, docs_methods_heise_final_corners_stage [EXTRACTED 1.00]
- **Twophase tables heuristic movement symmetry set** — docs_tables_kociemba_phase1_heuristic, docs_tables_kociemba_phase2_heuristic, docs_tables_movement_tables, docs_tables_symmetry_tables [EXTRACTED 1.00]
- **Human methods cfop/roux/zz/heise in fuzz benchmark** — benchmarks_results_full_cfop_method, benchmarks_results_full_roux_method, benchmarks_results_full_zz_method, benchmarks_results_full_heise_method [EXTRACTED 1.00]
- **Heuristic table family with corner, edgePermutation, kociemba phase1/phase2 and pieceCube movement variants** — tests_tables_hashes_heuristic_hashes, tests_tables_hashes_kociemba_tables, tests_tables_admissibility_admissibility_check, tests_tables_entrycompare_entrycompare_check [INFERRED 0.85]
- **WITH vs WITHOUT tables parity benchmark flow** — benchmarks_tables_bench_tables_benchmark, tests_tables_parity_scrambles_parity_scramble_set, tests_tables_parity_csv_parity_comparison, tests_tables_parity_parity_check [EXTRACTED 1.00]
- **Solvers to searcher to core cubie solving pipeline** — archify_architecture_rubik_solver_20261108_230835_candidate_cli, archify_architecture_rubik_solver_20261108_230835_candidate_human_solvers, archify_architecture_rubik_solver_20261108_230835_candidate_twophase, archify_architecture_rubik_solver_20261108_230835_candidate_idasearcher, archify_architecture_rubik_solver_20261108_230835_candidate_core_cubie [EXTRACTED 1.00]
- **Baseline final final2 benchmark comparison (seed42 50 scrambles)** — scratch_baseline_results_benchmark, scratch_final_results_benchmark, scratch_final2_results_benchmark, scratch_final3_summary_final_integration [EXTRACTED 1.00]
- **Roux ZZ Heise remigration onto shared IdaSearcher** — scratch_migration_roux_zz_heise_spec_remigration_spec, scratch_migration_roux_zz_heise_spec_stack_array_idasearcher, scratch_migration_roux_zz_heise_spec_pruning_wrapper [INFERRED 0.85]

## Communities (65 total, 9 thin omitted)

### Community 0 - "Tables Loading Discovery"
Cohesion: 0.05
Nodes (122): run_self_test(), binom_small(), array, Metric, path, size_t, string, string_view (+114 more)

### Community 1 - "ZZ Solver Pruning"
Cohesion: 0.05
Nodes (108): CubeState, centers, co, cp, eo, ep, uint8_t, allowed_moves_for_stage() (+100 more)

### Community 2 - "Heise Solver Pruning"
Cohesion: 0.06
Nodes (100): allowed_moves_for_stage(), buildBrDb(), buildCombinedPermDb(), buildCornerMoveTable(), buildCornerPermTable(), buildDOrientDb(), buildEdgeMoveTable(), buildEdgePermTable() (+92 more)

### Community 3 - "CFOP Solver Pruning"
Cohesion: 0.07
Nodes (86): allowed15(), allowed18(), allowed_moves_for_stage(), buildCornerDb(), buildEdgeDb(), buildOriBothDb(), buildOriCornerDb(), buildOriEdgeDb() (+78 more)

### Community 4 - "Tables Adapter Heuristics"
Cohesion: 0.06
Nodes (68): shared_ptr, get_adapter(), string, corner_h_name_local(), Metric, path, string, uint32_t (+60 more)

### Community 5 - "Facelet Conversion Validation"
Cohesion: 0.05
Nodes (73): atomic, ImVec4, mutex, expected, FaceletArray, FaceletError, string, string_view (+65 more)

### Community 6 - "Roux Solver Pruning"
Cohesion: 0.08
Nodes (72): allowed_moves_for_stage(), buildCombinedDb(), buildCornerMoveTable(), buildEdgeMoveTable(), buildLseEdgeMoveTable(), buildPdbs(), centreRotOf(), cmll_allowed_moves() (+64 more)

### Community 7 - "Searcher Validation Tests"
Cohesion: 0.11
Nodes (38): NextFn, bfsDb(), size_t, uint32_t, uint8_t, vector, pdbLookup(), BaselineParity (+30 more)

### Community 8 - "Solver Theory Documentation"
Cohesion: 0.06
Nodes (37): rubik-solver agent guide, Heise stage 3 corner-edge pairs solve edges and two corners, Heise stage 2 pair blocks to F2L-1, Heise stage 4 final corners commutator, Heise Method canonical stages, Ryan Heise Heise method proposer, SpeedSolving Wiki Heise method, Heise stage 1 build four squares (+29 more)

### Community 9 - "IDA Searcher Core"
Cohesion: 0.08
Nodes (24): Apply, Goal, Heuristic, S, SolveError, time_point, uint64_t, vector (+16 more)

### Community 10 - "Fast Apply Interface"
Cohesion: 0.10
Nodes (29): fast_apply(), heuristic_phase1(), heuristic_phase2(), expected, SolveError, time_point, uint64_t, uint8_t (+21 more)

### Community 11 - "Renderer Interaction Tests"
Cohesion: 0.07
Nodes (28): CameraZoomRotateClamp, CliAction, CliArgs, ClickDoesNotRotateDragDoes, FaceCentresAllFaces, HelpVersionExitBeforeGl, HiDpiPickScalesToFramebuffer, Highlight (+20 more)

### Community 12 - "ZZ Solver Tests"
Cohesion: 0.14
Nodes (28): RluPreservesEoline, allTokens(), applyTokens(), contains(), CancelledBehavior, MaxDepthRespected, mt19937, PredicatesOnSolved (+20 more)

### Community 13 - "Benchmark Statistics Tracking"
Cohesion: 0.07
Nodes (28): MethodStats, attempted, cancelled, database_missing, failures, failures_detail, incomplete, max_depth (+20 more)

### Community 14 - "Heise Solver Tests"
Cohesion: 0.14
Nodes (27): FinalCornersLeavesThreeCorners, Heise, PredicatesNestOnScramble, allTokens(), applyTokens(), contains(), CancelledBehavior, MaxDepthRespected (+19 more)

### Community 15 - "Roux Solver Tests"
Cohesion: 0.15
Nodes (27): LseOnlyMuAndPreservesBlocks, Roux, allTokens(), applyTokens(), contains(), CancelledBehavior, MaxDepthRespected, mt19937 (+19 more)

### Community 16 - "Architecture Documentation History"
Cohesion: 0.09
Nodes (27): rubik-solver Architecture: CLI, solvers and core cubie, CLI rubik-solver (src/main.cpp), Core cubie CubeState moves, Human solvers CFOP Roux ZZ Heise, IdaSearcher IDA* plus PDB, Refsim independent verification, Architecture candidate review1 (duplicate of candidate), Tables HTM mmap read-only (+19 more)

### Community 17 - "Animated Cube Replay"
Cohesion: 0.10
Nodes (18): AnimatedCube, colors_, completeCurrent, index_, moves_, pause, play, progress_ (+10 more)

### Community 18 - "Renderer Window Management"
Cohesion: 0.08
Nodes (21): GLFWwindow, Renderer, camera_, dragging_, glReady_, handleMouseButton, handleMouseMotion, height_ (+13 more)

### Community 19 - "CFOP Solver Tests"
Cohesion: 0.14
Nodes (25): Cfop, PllTablePreservesF2LAndOrientation, PllTableSolvesAllPllStates, allTokens(), contains(), CancelledBehavior, MaxDepthRespected, mt19937 (+17 more)

### Community 20 - "Move Parsing Formatting"
Cohesion: 0.14
Nodes (24): solved, all_face_moves(), apply(), apply_sequence(), expected, string, string_view, vector (+16 more)

### Community 21 - "Renderer GL Drawing"
Cohesion: 0.14
Nodes (25): setState, colorFor(), compile(), array, FaceletArray, cubiePositionForSticker(), faceNormal(), faceU() (+17 more)

### Community 22 - "3D Math Matrices"
Cohesion: 0.18
Nodes (21): cross(), cubieTransform(), dot(), length(), Mat4, m, matLookAt(), matPerspective() (+13 more)

### Community 23 - "Fuzz Benchmark Harness"
Cohesion: 0.23
Nodes (23): mt19937, SolveError, string, vector, error_name(), face_of(), join_tokens(), main() (+15 more)

### Community 24 - "Integration Replay Tests"
Cohesion: 0.10
Nodes (24): IntegrationBackground, IntegrationMirror, IntegrationReplay, IntegrationValidationGate, PerMethodReplayEqualsSolved, PreCancelledReturnsCancelledOrFast, SolvedCubeIsSolved, applyTokenInstant() (+16 more)

### Community 25 - "Core Interface Headers"
Cohesion: 0.30
Nodes (9): array, string, vector, stop_token, StageSolution, moves, stage_name, expected (+1 more)

### Community 26 - "Facelet Validation Tests"
Cohesion: 0.11
Nodes (23): ColorCount, CoreValidatorAgrees, CornerTwist, DuplicateCenter, DuplicatePiece, EdgeFlip, FaceletTest, InvalidCharacter (+15 more)

### Community 27 - "Orbit Camera Picking"
Cohesion: 0.16
Nodes (21): Vec3, OrbitCamera, distance, eye, forward, fovY, pickWindow, pitch (+13 more)

### Community 28 - "Cube Moves Tests"
Cohesion: 0.12
Nodes (19): AgreesWithCoreOnScrambles, AgreesWithCoreOnSolved, Compose, CoreScrambleVerifiedByReference, CubeTest, FaceletRoundtripRandom, FaceletRoundtripScrambles, IdentityMoves (+11 more)

### Community 29 - "Cube Rotation Basis"
Cohesion: 0.17
Nodes (18): Axis, map, base_permutation(), array, permutation_for_token(), rot_x(), rot_y(), rot_z() (+10 more)

### Community 30 - "Tables Discovery Tests"
Cohesion: 0.12
Nodes (16): BuildWinsWhenLocalAbsent, EmptyWhenNothingPresent, ExeTablesWhenCwdAbsent, ExplicitWins, LocalTablesBeatsBuild, testing::Test, path, DiscoveryFixture (+8 more)

### Community 31 - "Cube State Operations"
Cohesion: 0.16
Nodes (16): compose(), expected, string, string_view, vector, face_index(), identity(), is_solved() (+8 more)

### Community 32 - "Facelet Validation Core"
Cohesion: 0.17
Nodes (16): check_facelets(), expected, optional, SolveError, string_view, uint8_t, SolveError, string (+8 more)

### Community 33 - "CLI Args Parser"
Cohesion: 0.15
Nodes (16): Args, data_dir, facelets, has_random_depth, method, random_depth, scramble, seed (+8 more)

### Community 34 - "Layer Turn Animation"
Cohesion: 0.17
Nodes (16): currentAngleRad, currentToken, currentTurn, isTurningLayer, applyTurnToPoint(), optional, string, isInTurningLayer() (+8 more)

### Community 35 - "Reference Simulator"
Cohesion: 0.32
Nodes (13): apply_base(), apply_sequence(), apply_token(), cycle4(), array, string, string_view, vector (+5 more)

### Community 36 - "Validation Unit Tests"
Cohesion: 0.21
Nodes (12): DoubleSwapIsValidParity, FlippedEdgeSpecificError, ParitySwapSpecificError, RejectsBadLengthAndCharacter, RejectsWrongColourCount, SolvedAndScrambledAreValid, string, facelets_of() (+4 more)

### Community 37 - "Symmetry Table Layouts"
Cohesion: 0.18
Nodes (11): Phase1SymmetryLayout, corner_orient_bytes, edge_orient_bytes, edge_orient_off, equiv_bytes, equiv_off, middle_edges_bytes, middle_edges_off (+3 more)

### Community 38 - "Phase Two Layouts"
Cohesion: 0.18
Nodes (11): Phase2SymmetryLayout, corner_perm_bytes, e_edge_bytes, e_edge_off, equiv_bytes, equiv_off, min_orient_bytes, min_orient_off (+3 more)

### Community 39 - "Benchmark Args Parsing"
Cohesion: 0.18
Nodes (11): Args, auto_mode, data_dir, facelets, has_random_depth, method, random_depth, scramble (+3 more)

### Community 40 - "Sticker Quad Struct"
Cohesion: 0.18
Nodes (11): StickerQuad, center, col, cubiePos, face, half, index, normal (+3 more)

### Community 41 - "Benchmark Config"
Cohesion: 0.20
Nodes (10): Config, help, human_depth, human_max_depth, human_timeout_ms, methods, output, seed_base (+2 more)

### Community 42 - "Cubie Move Data"
Cohesion: 0.22
Nodes (10): CubieMove, co, cp, eo, ep, array, MoveData, face (+2 more)

### Community 43 - "Solver Method Config"
Cohesion: 0.22
Nodes (9): SolverFn, MethodCfg, fn, max_depth, name, scramble_depth, seed_offset, thread_count (+1 more)

### Community 44 - "Solve Options"
Cohesion: 0.25
Nodes (8): SolveOptions, max_depth, thread_count, timeout_ms, defaultOptions(), defaultOptions(), defaultOptions(), defaultOptions()

### Community 45 - "Fuzz Benchmark Documentation"
Cohesion: 0.33
Nodes (7): Baseline scramble set (25 face moves per scramble), Fuzz benchmark 1000 trials per method (SPEC section 10), Refsim verification: solved+scramble+solution==solved, timing excludes scramble+verify and warmup, Fuzz benchmark reduced run 200 trials per method, Fuzz benchmark reduced run 50 trials per method including optimal, optimal method (scramble depth 10, timeout 60000, max depth 20), Fuzz warmup ok per method; acceptance met >=99% verified per method

### Community 46 - "ANSI Terminal Visualiser"
Cohesion: 0.43
Nodes (7): ansi_color(), string, main(), print_help(), print_net_clean(), read_key(), stage_predicate()

### Community 47 - "Heuristic Tables Documentation"
Cohesion: 0.33
Nodes (6): Admissibility check: HTM set valid, BFS depth 1-6, views corner/edgePerm/phase1/sym, PASS, Entrycompare N=1000000 seed 777001: corner equal, edgePerm PDB dominates, phase1 histogram max 12, Heuristic table hash list: corner/edgePermutation/kociemba phase1+phase2/pieceCube movement ATM/HTM/QTM/STM with sizes and hashes, Kociemba heuristic and symmetry tables (phase1/phase2, large phase1 variants), Per-scramble parity comparison WITH vs WITHOUT tables: depth, len, ms, verified, Parity check PASS both_ok=500; heuristic source tables HTM + data PDBs (max) with tables fallback

### Community 48 - "Glossary Domain Documentation"
Cohesion: 0.40
Nodes (5): Domain docs agent skill, Architecture Decision Records docs/adr, Domain Docs, GLOSSARY-MAP.md multi-context map, GLOSSARY.md

### Community 49 - "Issue Tracker Documentation"
Cohesion: 0.40
Nodes (5): Issue tracker agent skill, Scratch feature directory .scratch/<feature>/, Local Markdown issue tracker, Triage Status line, Wayfinder map and child tickets

### Community 50 - "Fuzz Failure Records"
Cohesion: 0.40
Nodes (5): FailureRecord, ms, reason, scramble, trial

### Community 51 - "Solution Stage Definitions"
Cohesion: 0.50
Nodes (3): Solution, stages, expectStagePredicates()

### Community 52 - "Table File Spec"
Cohesion: 0.50
Nodes (4): TableFileSpec, expected_bytes, filename, sha256_hex

### Community 53 - "Tables Benchmark Documentation"
Cohesion: 0.67
Nodes (3): Tables benchmark WITH vs WITHOUT tables, seed 888002 count 500 depth uniform 1..11, Tables benchmark optimal SAME scrambles seed 888002 count 500 depth uniform 1..11, Parity scramble set seed 888002 count 500 depth uniform 1..11

## Ambiguous Edges - Review These
- `Baseline benchmark seed42 50 scrambles depth25 (cfop 50/50 roux 49/50 zz 50/50 heise 47/50 optimal 0/50 twophase 0/50 Timeout)` → `Final2 benchmark (cfop 50/50 median 3146ms, roux 49/50, zz 49/50, heise 44/50, twophase 50/50 median 426ms)`  [AMBIGUOUS]
  .scratch/final2/results.json · relation: conceptually_related_to

## Knowledge Gaps
- **398 isolated node(s):** `trials`, `seed_base`, `human_depth`, `human_timeout_ms`, `human_max_depth` (+393 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 595 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **9 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What is the exact relationship between `Baseline benchmark seed42 50 scrambles depth25 (cfop 50/50 roux 49/50 zz 50/50 heise 47/50 optimal 0/50 twophase 0/50 Timeout)` and `Final2 benchmark (cfop 50/50 median 3146ms, roux 49/50, zz 49/50, heise 44/50, twophase 50/50 median 426ms)`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._
- **Why does `CubeState` connect `ZZ Solver Pruning` to `Tables Loading Discovery`, `Heise Solver Pruning`, `CFOP Solver Pruning`, `Tables Adapter Heuristics`, `Facelet Conversion Validation`, `Roux Solver Pruning`, `Fast Apply Interface`, `ZZ Solver Tests`, `Heise Solver Tests`, `Roux Solver Tests`, `CFOP Solver Tests`, `Move Parsing Formatting`, `Fuzz Benchmark Harness`, `Integration Replay Tests`, `Core Interface Headers`, `Cube State Operations`, `Validation Unit Tests`, `ANSI Terminal Visualiser`, `Solution Stage Definitions`?**
  _High betweenness centrality (0.228) - this node is a cross-community bridge._
- **Why does `solved` connect `Move Parsing Formatting` to `Tables Loading Discovery`, `ZZ Solver Pruning`, `Validation Unit Tests`, `Facelet Conversion Validation`, `Tables Adapter Heuristics`, `Renderer Interaction Tests`, `ZZ Solver Tests`, `ANSI Terminal Visualiser`, `Heise Solver Tests`, `Roux Solver Tests`, `CFOP Solver Tests`, `Fuzz Benchmark Harness`, `Integration Replay Tests`, `Facelet Validation Tests`, `Cube Moves Tests`, `Cube State Operations`?**
  _High betweenness centrality (0.081) - this node is a cross-community bridge._
- **Why does `TEST()` connect `Renderer Interaction Tests` to `Layer Turn Animation`, `Animated Cube Replay`, `Renderer Window Management`, `Move Parsing Formatting`, `Renderer GL Drawing`, `3D Math Matrices`, `Integration Replay Tests`, `Orbit Camera Picking`, `Cube State Operations`?**
  _High betweenness centrality (0.073) - this node is a cross-community bridge._
- **Are the 3 inferred relationships involving `CubeState` (e.g. with `to_array()` and `to_array()`) actually correct?**
  _`CubeState` has 3 INFERRED edges - model-reasoned connections that need verification._
- **Are the 44 inferred relationships involving `TEST()` (e.g. with `is_solved()` and `parse_facelets()`) actually correct?**
  _`TEST()` has 44 INFERRED edges - model-reasoned connections that need verification._
- **What connects `trials`, `seed_base`, `human_depth` to the rest of the system?**
  _398 weakly-connected nodes found - possible documentation gaps or missing edges._