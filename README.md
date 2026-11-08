# rubik-solver

C++23 3×3 Rubik's cube solver: **CFOP, Roux, ZZ, Heise, and optimal (IDA\* + pattern databases)**. Cite: Korf (1997), *"Finding Optimal Solutions to Rubik's Cube Using Pattern Databases"*, AAAI/IAAI.

Built with CMake (C++23, `-Wall -Wextra -Wpedantic`). GoogleTest via FetchContent is the only dependency. Pattern databases are generated into `data/` (~108 MB) and are not committed.

## Build (Windows/MinGW g++ 16.2, measured)

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -V
.\build\build_pattern_db.exe --data-dir data   # ~78 s measured; 5-10 min typical
.\build\rubik-solver.exe --method cfop --scramble "R U R' U'"
```

Toolchain measured 2026-11-08: CMake 4.4.3, g++ 16.2.0 (MSYS2 UCRT64), ninja 1.13.2. `std::expected`/`std::format`/`std::flat_map`/`std::stop_token` verified; **`std::print` fails to link on MinGW** (`__open_terminal` undefined), so the code uses `std::format` + `std::cout` throughout.

## CLI

```
--method <cfop|roux|zz|heise|optimal|twophase> [--scramble "R U R' ..."] [--facelets <54 chars>] [--random-depth N] [--seed N] [--data-dir DIR] [--tables-dir DIR] [--timeout-ms N]
```

Output: scramble, per-stage moves, total move count (each token counts as 1), elapsed time, and `verified: yes/no` from the independent reference simulator.

Example:

```
method: cfop
scramble: R U R' U'
solution:
  cross: 
  f2l_1: R' F R F'
  ...
total_moves: 27
elapsed_ms: 3764   # first run includes PDB warmup; steady-state is much lower
verified: yes
```

## Methods

- **CFOP:** Cross → F2L (4 pairs) → OLL → PLL
- **Roux:** first block (1×2×3) → second block → CMLL → LSE (M + U only)
- **ZZ:** EOLine → ZZF2L (R/L/U only) → COLL → EPLL (ZBLL optional)
- **Heise:** squares → F2L-1 → corner-edge pairs → final corners (see `docs/methods/heise.md`)
- **Optimal:** IDA\* with heuristic = max of 4 admissible PDBs (corner 8!×3⁷ = 88,179,840 entries ~44 MB; three edge-6 PDBs of 42,577,920 entries ~21 MB each; ~108 MB total). Never reported as "optimal" unless IDA\* completed; on timeout → `Timeout` error.

## Benchmarks (this machine, real runs)

`benchmarks/fuzz.exe` (single process, `seed_base=20261108`, human depth 25, optimal depth 10, human timeout 30 s, warmup excluded). Full 1000-trial run, 2026-11-08 (`benchmarks/results_full.txt`, 4577 s wall):

| method | trials | success | avg moves (min–max) | avg ms | median ms | p95 ms | max ms |
|---|---|---|---|---|---|---|---|
| cfop | 1000 | 100% (1000/1000) | 54.2 (36–63) | 300 | 125 | 896 | 14170 |
| roux | 1000 | 98.9% (989/1000, 11 timeouts at 30 s) | 36.6 (27–45) | 1954 | 322 | 8541 | 26908 |
| zz | 1000 | 100% (1000/1000) | 48.3 (29–59) | 206 | 35 | 942 | 12299 |
| heise | 1000 | 99.9% (999/1000, 1 timeout at 30 s) | 75.5 (33–98) | 1753 | 237 | 8828 | 28697 |
| optimal (depth ≤10) | 1000 | 100% (1000/1000) | 9.6 (4–10) | 1.0 | 0.8 | 1.9 | 9.7 |

Verify failures = 0 for every method (every returned solution re-checked with the reference simulator). Roux/heise have heavy tails (p95 ~8.5–8.8 s); timeouts are the 30 s limit, not wrong solutions (slowest roux success 26.9 s; slowest heise success 28.7 s). Warmup (PDB/table builds, excluded above): cfop 13.3 s, roux 0.5 s, zz 0.9 s, heise 6.4 s, optimal 4.4 s. The earlier 200-trial spot-check is in `benchmarks/results.txt` (same rates).

## Tests

`ctest` — 6 suites, 75 tests (core 19, optimal 13, cfop 11, roux 10, zz 11, heise 11). PDB admissibility ≤ depth 6 (pattern space) / ≤5 (full cube, ~700k states), optimal matches BFS ≤ depth 7, invalid facelets (twist/flip/parity) rejected with specific errors. `git ls-files` shows no `*.db`.

## Visualiser (P5 Option 1 — terminal ANSI)

```
.\build\rubik-visualiser.exe --method cfop --scramble "R U"          # press n/p to step stages, q to quit
.\build\rubik-visualiser.exe --method roux --random-depth 10 --seed 42 --auto  # print all stages (scripts)
```

Step-through cube net (`U` on top, `L F R B` middle, `D` bottom, URFDLB order) with ANSI colours, reusing `to_facelets()` + the independent refsim. Shows each stage's cube state, its moves, `predicate [stage]: yes/no` (solver goal predicate), `refsim matches state: yes/no`, and final `solved: yes/no`. `--auto` prints every stage without waiting for keys (verified 2026-11-08: all 5 methods × scramble + random-depth 10 → predicate/refsim/solved all `yes`). Interactive mode reads single keys via `_getch()` on Windows (no new dependencies). GLFW + OpenGL 3D is deferred until stage logic is validated further.

## External tables (`tables\`, gitignored)

Supplied pattern tables (James Cameron solver, licence **unknown** — see `docs/tables.md` for SHA-256 manifest + format). `optimal` now takes `max(data\ PDBs, tables HTM set)`; missing/truncated `tables\` falls back to `data\` with a clear message. New method `twophase` (phase1→G1→phase2) uses the same tables.

| config | successes | avg ms | median ms | p95 ms | max ms |
|---|---|---|---|---|---|
| with `tables\` | 500 | 0.94 | 0.36 | 3.40 | 26.69 |
| without (`data\` only) | 500 | 0.81 | 0.35 | 2.63 | 33.46 |

(Same 500 scrambles, seed 888002, depth 1..11; identical optimal lengths, 0 verify-fails. At depth ≤11 the tables add no speedup — the win is admissible strength at depth, not latency. Full BFS ≤6: 8,240,087 states, 0 violations; 1M-index comparison vs `data\`: corner identical 1M/1M. See `benchmarks/tables_bench.txt` + `tests/tables/`.)

## Docs

`docs/SPEC.md` (phases, interface freeze), `docs/methods/heise.md` (Heise canonical stages + sources), `docs/tables.md` (tables manifest + format + verification). Issue tracker: local markdown under `.scratch/`. Domain docs: single-context (`GLOSSARY.md` + `docs/adr/`, created lazily).

## License / design note

Algorithm/database design inspired by crux (https://github.com/aravinth-kanesh/crux); **no code copied** (crux shows no license). All implementations are original on the frozen core cubie model.
