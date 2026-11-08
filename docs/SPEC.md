# rubik-solver — SPEC (P0 Recon)

Date: 2026-11-08. Status: P0 draft — STOPPED for user approval before writing implementation code.

## 1. Goal & Methods

C++23 CLI that solves a 3x3 cube with one user-selected method per run: `cfop`, `roux`, `zz`, `heise`, or `twophase` (Kociemba two-phase + supplied tables). One common interface, method registry name → solver.

## 2. Toolchain (measured 2026-11-08)

- CMake 4.4.3, g++ (MSYS2 UCRT64) 16.2.0, clang 22.1.8, ninja 1.13.2, ctest 4.4.3, git 2.56.0. `gh`/`cl` absent (MSVC unavailable; MinGW gcc/clang only).
- Flags: `-std=c++23 -Wall -Wextra -Wpedantic`, zero warnings required.
- C++23 probe results (`build/probe/probe2.cpp`, `probe3.cpp` compiled + ran on g++ 16.2):
  - OK: `std::expected`, `std::format`, `std::flat_map`/`std::flat_set`, `std::stop_token`/`std::jthread`, `std::filesystem`, `<atomic>`.
  - FAIL: `std::print` → link error `undefined reference to std::__open_terminal` on MinGW (both g++ and clang in UCRT64). **Fallback: `std::format` + `std::cout` (never `std::print`).** Recorded per hard rule.
- Dependencies: GoogleTest only (FetchContent). No other deps without asking. No internet at build test time assumed; FetchContent pinned version TBD in P1.

## 3. MCP inventory (connected servers, via `search`)

Present: `canva` (49 tools), `concat` (video editor, 42 tools), `crawl4ai` (scrape/crawl), `cua-driver` (GUI automation), `duckduckgo` (search/expand_link), `markitdown` (URI→markdown), `nvidia-app`, `opencode` (session/worktree/MCP resources), `pdf-mcp` (PDF extraction), `video-tools`, `weather`. No library-docs MCP, no GitHub repo MCP, no `gh` CLI.

Usage (read-only): `crawl4ai.scrape` / `duckduckgo.search` / `markitdown` / `pdf-mcp` for wiki + Korf + cppreference; `opencode` only for session/worktree ops, never outer writes. Never use any tool for pushes/issues/PRs/messages; never put credentials in tool calls.

## 4. Layout

```
CMakeLists.txt                  # root, only main agent edits
.gitignore                      # data/ + build/
src/core/                       # CubeState (CP,CO,EP,EO), 54-facelet conv, 18 face moves + M/E/S (+ ' and 2) + rotations, WCA parse/format, validation, reference simulator (tests only)
src/solvers/{cfop,roux,zz,heise,twophase}/
src/main.cpp                    # CLI
tests/                          # CTest, GoogleTest
docs/SPEC.md, docs/methods/heise.md
data/                           # generated DBs, never committed (# ~105 MB)
```

Subagent boundaries: P1 `core-engineer` → `src/core/`+tests; P2 `pdb-engineer` → `src/solvers/twophase/`+tables+admissibility; P3 solver-engineers (one per human method, after interface freeze) → own dirs only. Main agent owns root CMake, common interface, integration. Max 3 concurrent. Check official OpenCode docs for agent-definition format/location before creating any agent file — do not guess.

## 5. Common interface (freeze after P1)

```cpp
// C++23, std::expected + std::stop_token verified OK.
std::expected<Solution, SolveError> solve(const CubeState&, const SolveOptions&);
struct Solution { /* ordered {stage_name, moves} */ };
struct SolveOptions { /* timeout, max depth, thread count, std::stop_token */ };
enum class SolveError { Unsolvable, Timeout, Cancelled, DatabaseMissing, /* ... specific facelet errors ... */ };
```

## 6. Core requirements

- Cubie state CP/CO/EP/EO + 54-facelet conversion (face order TBD: URFDLB + per-face layout documented; fixed in P1).
- Moves: 18 face moves + M/E/S and whole-cube rotations as needed; WCA notation parse/format; move counts count each token (`R`, `R'`, `R2`, `M`) as 1.
- Validation: colour counts (9 per colour), corner twist sum ≡ 0 mod 3, edge flip sum ≡ 0 mod 2, permutation parity (corner parity == edge parity); specific error for twisted corner / flipped edge / parity swap.
- Independent facelet-permutation reference simulator, tests only.

## 7. Method definitions

- **CFOP:** Cross → F2L (4 pairs) → OLL → PLL. Confirmed: https://www.speedsolving.com/wiki/CFOP_method
- **Roux:** First block (1×2×3) → second block (1×2×3) → CMLL → LSE. LSE uses only M and U moves. Confirmed: https://www.speedsolving.com/wiki/Roux_method
- **ZZ:** EOLine → ZZF2L (left+right blocks, only R/L/U) → COLL then EPLL (ZBLL optional). Wiki: https://www.speedsolving.com/wiki/ZZ_method — objective picks the ZZ-CE/VH variant (EOLine + COLL/EPLL); EOCross is modern-faster but out of scope unless user changes spec.
- **Heise:** canonical 4 stages per `docs/methods/heise.md` (SpeedSolving Wiki + Ryan Heise site agree; Old Heise / Speed-Heise are variants, out of scope).
- All human methods: assert stage predicate after each stage; assert restricted move set per stage; per-stage IDA*/BFS + small pruning allowed; algorithm tables allowed only if simulation-verified; fingertricks not required.

## 8. Two-phase solver (Kociemba)

Design (inspired by the upstream public solver `tables\` set — licence unknown, see `docs/tables.md`; cite Kociemba 2008 two-phase method):

- Phase 1 (any of the 18 face moves) reaches G1 (corner/edge orientations solved, slice cubies in place); phase 2 (10 G1-preserving moves) reaches solved. Each phase is IDA* with a tables-backed admissible heuristic.
- Heuristics come from the supplied `tables\` set (corner/edge-permutation/phase-1/phase-2 HTM helpers + symmetry + movement tables, mmap'd read-only). A missing or invalid `tables\` set degrades the heuristic to h=0 (still correct, much slower on deep scrambles) and the loader prints a fallback message; `SolveError::DatabaseMissing` is returned when the tables adapter is unavailable.
- Staged solutions are reported as two stages (`phase1`, `phase2`); on timeout → `Timeout` error.

## 9. CLI

`--method`, `--scramble "R U R' …"`, `--facelets <54 chars>`, `--random-depth N`, `--seed N`, `--data-dir`, `--timeout-ms`. Output: scramble, per-stage moves, total move count, elapsed time, `verified: yes/no` from reference simulator.

## 10. Testing & acceptance

- `ctest` passes; release build zero warnings.
- 1,000 random scrambles/method (depth 25 human) solve per reference simulator; stage predicates + move-set restrictions hold; each tables heuristic ≤ true BFS distance for all states within depth ≤6; twophase solutions verify against the reference simulator (solutions are valid, not guaranteed length-minimal); invalid facelets (twisted/flipped/parity) rejected with specific error; `git ls-files` shows no `*.db`; README benchmark table from real runs on this machine.

## 11. Actions / stops

Forbidden: `git push`, history rewrites, `sudo`, global installs, out-of-tree edits, unlisted features. Run DB builder in background with log file. Stop + ask: before deleting any file, adding deps beyond GoogleTest, changing frozen interface, resolving conflicting method definitions, starting P5 (visualiser — no GUI dep without approval), if an error persists after 2 attempts, if DB build >30 min, or if work leaves scope.

## 12. P0 evidence

- Toolchain probe: g++ 16.2.0 / clang 22.1.8 / CMake 4.4.3; `probe2`/`probe3` exit 0, `std::print` link fails (see §2).
- Researcher subagent `ses_e46c4366dffeOUPXTP6hJRBcSN` completed (read-only, no files changed) — Heise 4 stages agree, crux layout recorded, ZZ/CFOP/Roux confirmed.
- Setup config: `AGENTS.md`, `docs/agents/issue-tracker.md`, `docs/agents/domain.md` written (Local markdown, single-context).

## References

- Korf, R.E. (1997). *Finding Optimal Solutions to Rubik's Cube Using Pattern Databases*. AAAI/IAAI. (Cited for the PDB/IDA* concept; the removed solver is not in this tree — see git history.)
- SpeedSolving Wiki: CFOP / Roux / ZZ / Heise (URLs in §7 and `docs/methods/heise.md`).
- crux (layout only, no code copied): https://github.com/aravinth-kanesh/crux
