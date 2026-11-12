# Korf-Style Stage Pattern Databases — Design Document

**Branch**: `perf/korf-stage-pdb`  
**Date**: 2026-11-10  
**Status**: Implementation phase (Wave 2 — Foundation Architect)

---

## 1. Overview

This document specifies the shared infrastructure for Korf-style stage pattern databases (PDBs) across the four human methods (CFOP, Roux, ZZ, Heise) and the two-phase solver. The infrastructure is designed as a **generic, solver-agnostic layer** that solvers consume via a `StageConfig` extension point. Solvers never edit shared files; they register their stage-specific configuration and the shared infra builds, loads, and queries the PDBs.

---

## 2. Per-Stage Admissibility Argument

### 2.1 Principle

A pattern database heuristic `h(state)` is **admissible** iff for every reachable state `s` in the stage's state space:
```
h(s) ≤ true_distance(s, stage_goal)
```
where `true_distance` is the optimal solution length under the stage's **restricted move set**.

### 2.2 Construction Method: Backward BFS from Stage Goal

Each PDB is built by **breadth-first search backward from the stage goal state(s)** using **exactly the stage's allowed move set**. This guarantees:
- Every entry stores the true optimal distance to the goal under the stage moves
- The heuristic never overestimates (BFS explores in increasing distance order)
- Sentinel value `0xF` (unreached) maps to `0` at lookup time, preserving admissibility

### 2.3 Disjoint vs. Overlapping Pattern Sets

| Combination Rule | Condition | Admissibility Proof |
|------------------|-----------|---------------------|
| **Sum** `h₁ + h₂` | Pattern piece sets are **disjoint** (no shared cubies) | Moves affecting one set don't affect the other; true distances add |
| **Max** `max(h₁, h₂)` | Pattern piece sets **overlap** (share ≥1 cubie) | Each is a lower bound; maximum of lower bounds is a lower bound |

**Disjointness is a structural property of the pattern definition**, verified at `StageConfig` registration time via a `DisjointGroup` trait.

### 2.4 Per-Method Stage Admissibility Summary

#### CFOP
| Stage | Disjoint Groups (Sum) | Overlapping (Max) |
|-------|----------------------|-------------------|
| Cross | — | Single PDB (`cross18`) |
| F2L (pair k) | Cross edges (4,5,6,7) + Pair k (corner+edge) | — |
| OLL | — | All PDBs overlap (`oriC`/`oriE`/`oriBoth` with piece-specific) |
| PLL | — | `pllPerm` overlaps `pllC`+`pllE` |

#### Roux
| Stage | Disjoint Groups (Sum) | Overlapping (Max) |
|-------|----------------------|-------------------|
| First Block | — | Single PDB (`fb`) |
| Second Block | FB + SB (disjoint cubies) | — |
| CMLL | FB + SB + CMLL (U-corners disjoint from FB/SB) | — |
| LSE | — | Single joint PDB (`lse`: 6 edges + centres + U-corner rot) |

#### ZZ
| Stage | Disjoint Groups (Sum) | Overlapping (Max) |
|-------|----------------------|-------------------|
| EOLine | — | Single PDB (`eoline`) |
| Left | — | EOLine overlaps Left (shared edges 6,9,10) |
| Right | Left + Right (disjoint blocks) | EOLine overlaps both |
| COLL | EOLine + Left + Right + COLL (U-corners disjoint) | — |
| EPLL | — | `perm` overlaps `coll`+`uedge` |

#### Heise
| Stage | Disjoint Groups (Sum) | Overlapping (Max) |
|-------|----------------------|-------------------|
| Squares | D-edges + D-corner-ori (disjoint) | — |
| F2L-1 | D-edges + D-corners(4,5,6) + mid-edges + U-edge-ori + 3 pairs (all disjoint) | — |
| Pairs | Above + BR edge + U-edges + DRB+URF corners (disjoint from D/mid) | `pairPerm`/`uEdgeCorner` overlap components |
| Final | Above + final 3 corners (disjoint) | `finalPerm` overlaps components |

---

## 3. Table Sizes & Storage Budget

### 3.1 Storage Format: Nibble-Packed (4 bits/entry)

- Max stage depth ≤ 20 (fits in 4 bits; 0xF = sentinel = unreached)
- 2 entries per byte: low nibble = even index, high nibble = odd index
- File size = `ceil(num_states / 2)` bytes
- No header, no magic — raw binary blob (compatible with existing `pdbLookup`)

### 3.2 Projected New PDB Sizes (Korf Upgrade)

| Method | Stage | Pattern | States | Packed Bytes | Move Set |
|--------|-------|---------|--------|--------------|----------|
| **CFOP** | Cross | 4 D-edges (pos+ori) | 190,080 | 95 KB | 18 |
|  | F2L pair | 1 corner+1 edge (pos+ori) | 576 | 288 B | 18 |
|  | OLL | 4 U-corners (pos+ori) | 136,080 | 68 KB | 15 |
|  | OLL | 4 U-edges (pos+ori) | 190,080 | 95 KB | 15 |
|  | OLL | All 8 corners ori | 2,187 | 1 KB | 15 |
|  | OLL | All 12 edges ori | 2,048 | 1 KB | 15 |
|  | PLL | 4 U-corners (pos+ori) | 136,080 | 68 KB | 18 |
|  | PLL | 4 U-edges (pos+ori) | 190,080 | 95 KB | 18 |
|  | PLL | U-corner perm × U-edge perm | 19,958,400 | ~10 MB | 18 |
| **Roux** | FB | 2 corners × 3 edges (pos+ori) | 5,322,240 | ~2.5 MB | 18 |
|  | SB | 2 corners × 3 edges (pos+ori) | 5,322,240 | ~2.5 MB | 18 |
|  | CMLL | 4 U-corners (pos+ori), 4 U-rot sources | 136,080 | 68 KB | 18 |
|  | LSE | 6 edges (pos+ori) × centre rot (4) × U-corner rot (4) | 737,280 | 360 KB | 6 (M/U) |
| **ZZ** | EOLine | 12 edges EO (2¹¹) × DF/DB pos (P(12,2)) | 270,336 | 135 KB | 18 |
|  | Left | 2 corners × 3 edges (pos+ori) | 5,322,240 | ~2.5 MB | 9 (R/L/U) |
|  | Right | 2 corners × 3 edges (pos+ori) | 5,322,240 | ~2.5 MB | 9 (R/L/U) |
|  | COLL | 4 U-corners (pos+ori) | 136,080 | 68 KB | 9 |
|  | EPLL | 4 U-edges (pos+ori) | 190,080 | 95 KB | 9 |
|  | EPLL | U-corner perm × U-edge perm | 19,958,400 | ~10 MB | 9 |
| **Heise** | Squares | 4 D-edges (pos+ori) | 190,080 | 95 KB | 18 |
|  |  | 3 D-corners (4,5,6) pos+ori | 9,072 | 4.5 KB | 18 |
|  | F2L-1 | 3 mid-edges (8,9,10) pos+ori | 10,560 | 5 KB | 18 |
|  |  | 4 U-edges pos+ori | 190,080 | 95 KB | 18 |
|  |  | 4 F2L pairs (each 576) | 576×4 | ~1 KB | 18 |
|  | Pairs | BR edge (11) | 24 | 12 B | 18 |
|  |  | URF corner | 24 | 12 B | 18 |
|  |  | 2 corners (7,0) pos+ori | 504 | 252 B | 18 |
|  |  | 5 edges perm × 2 corners perm | 5,322,240 | ~2.5 MB | 18 |
|  | Final | 3 corners (1,2,3) pos+ori | 9,072 | 4.5 KB | 18 |
|  |  | 4 U-edges perm × 3 corners perm | 3,991,680 | ~2 MB | 18 |
| **Two-Phase** | Phase 1 | G1 coordinates (twist/flip/middle) | 201,553,920 | ~100 MB | 18 |
|  | Phase 2 | G1→solved (corner/edge perm) | 1,339,269,120 (2-bit) | ~335 MB | 10 |

### 3.3 Total Budget

| Category | Estimate |
|----------|----------|
| **New human-method PDBs** (CFOP+Roux+ZZ+Heise) | **~60 MB** |
| **Two-phase tables** (existing `tables\` set, not new) | 4.3 GB (external, not counted in our build) |
| **Move tables** (per stage move set, in RAM) | ~3 MB per method |
| **Total new on-disk** | **< 100 MB** (well under 1 GB limit) |

---

## 4. Build-Time Budget

### 4.1 BFS Build Complexity

- Each PDB: single backward BFS from goal state(s)
- Visited states = full pattern state space (see table above)
- Branching factor = stage move set size (6–18)
- Parallelization: **per-move parallel expansion** within each BFS layer (embarrassingly parallel)

### 4.2 Target: < 15 Minutes Total Build Time

| PDB Family | States | Est. Time (single-thread) | Parallel (8-core) |
|------------|--------|---------------------------|-------------------|
| Corner (88M) | 88,179,840 | ~8 min | ~1.5 min |
| Edge 6-of-12 (42M each) | 42,577,920 | ~3 min | ~45 sec |
| PLL/EPLL perm (20M) | 19,958,400 | ~2 min | ~30 sec |
| Roux FB/SB (5.3M each) | 5,322,240 | ~30 sec | ~5 sec |
| ZZ Left/Right (5.3M each) | 5,322,240 | ~30 sec | ~5 sec |
| Heise pairPerm/finalPerm (5M/4M) | ~5M | ~30 sec | ~5 sec |
| All others (< 1M) | — | < 10 sec each | < 2 sec |

**Total parallel build estimate: ~3–4 minutes** (well under 15 min budget).

### 4.3 Stop Condition

> **If any single PDB build step exceeds 15 minutes → write BLOCKED report, stop and ask.**

---

## 5. Shared Infrastructure Components

### 5.1 Generic Stage-PDB Builder (`src/tables/stage_pdb.{hpp,cpp}`)

```cpp
// Backward BFS from stage goal(s) using stage move set
template <typename State, typename IndexFn, typename MoveApply>
std::vector<uint8_t> buildStagePdb(
    std::size_t num_states,
    const std::vector<uint32_t>& goal_indices,
    const std::vector<int>& stage_moves,
    IndexFn&& index_fn,           // State -> pattern index
    MoveApply&& apply_move        // (State, move) -> State
);
```

- Uses existing `bfsDb` infrastructure from `pdb.hpp`
- Coordinate move tables for O(1) index updates (built per stage move set)
- Nibble-packed output (4 bits/entry), max depth ≤ 20

### 5.2 Max-Combinator Heuristic (`src/core/heuristic.hpp`)

```cpp
// Trait-based combination: sum for disjoint, max for overlapping
template <typename State>
int combinedHeuristic(const State& s,
                      const std::vector<PdbLookup>& pdbs,
                      const std::vector<DisjointGroup>& groups);
```

- `DisjointGroup` = set of PDB indices tracking disjoint cubies
- Within group: **sum** (admissible because pieces don't interact)
- Across groups: **max** (admissible because overlapping)
- Verified at registration: disjoint groups must have provably disjoint piece sets

### 5.3 Move-Pruning Table (`src/tables/move_pruning.{hpp,cpp}`)

```cpp
// Per stage move set: redundant/opposite move elimination
struct MovePruningTable {
    // allowed_moves[i] = move index in global 18-move list
    // Returns true if move `curr` is redundant after `prev`
    bool isRedundant(int prev_move, int curr_move) const;
    
    // Canonical ordering for opposite faces: lower face index first
    // Face indices: U=0, D=1, F=2, B=3, L=4, R=5
    // Opposite pairs: (U,D), (F,B), (L,R)
};
```

- Built once per stage move set at solver init
- Reduces branching factor from 18 → ~13.3 on average
- No optimal solutions lost (opposite-face moves commute)

### 5.4 Root-Split Parallelism in `IdaSearcher` (`src/core/searcher.hpp`)

**Already implemented** (see lines 136–175). Extends with:
- **Early abort via `std::atomic<bool> found_flag`** shared across threads
- Workers check `found_flag` at each node (every 1024 nodes, same as deadline poll)
- When any thread finds a solution at current threshold, all others abort
- Main thread collects first solution (guaranteed optimal at this threshold)

### 5.5 Extension Point: `StageConfig` (`src/tables/stage_config.hpp`)

```cpp
struct Pattern {
    std::string name;
    std::vector<int> piece_indices;     // Cubie indices tracked (corners 0-7, edges 0-11)
    bool track_permutation;             // True = pos+ori, False = ori only
    std::size_t num_states;             // Pattern state space size
};

struct DisjointGroup {
    std::vector<int> pattern_indices;   // Indices into patterns[] that are disjoint
    // Verified: piece sets of these patterns have empty intersection
};

struct StageConfig {
    std::string stage_name;
    std::vector<Pattern> patterns;              // All PDB patterns for this stage
    std::vector<DisjointGroup> disjoint_groups; // Proven disjoint groups (sum within, max across)
    std::vector<int> allowed_moves;             // Global move indices (subset of 18)
    std::function<bool(const CubeState&)> goal_predicate; // Stage goal test
    std::vector<CubeState> goal_states;         // Goal state(s) for backward BFS (multi-source)
};
```

**Usage**: Each solver registers `StageConfig` for its stages. Shared infra consumes it to:
1. Build coordinate move tables for the stage's move set
2. Run backward BFS from `goal_states` to build PDBs
3. Construct move-pruning table for `allowed_moves`
4. Wire heuristic combinator via `disjoint_groups`

---

## 6. Integration with Existing Code

### 6.1 Files Extended (Not Duplicated)

| File | Extension |
|------|-----------|
| `src/core/searcher.hpp` | Add early-abort atomic flag to root-split parallelism |
| `src/core/pdb.hpp` | Reuse `bfsDb`, `pdbLookup`; add nibble-packed write helper |
| `src/tables/adapter.hpp` | Add `StagePdbLoader` for new PDB format (headerless nibble) |
| `src/tables/tables_loader.hpp` | Reuse `MappedFile` for mmap loading |

### 6.2 Files Created (Shared Infra)

| File | Purpose |
|------|---------|
| `src/tables/stage_pdb.hpp/cpp` | Generic backward BFS builder |
| `src/tables/move_pruning.hpp/cpp` | Move-pruning table per stage move set |
| `src/tables/stage_config.hpp` | `StageConfig` extension point |
| `src/core/heuristic.hpp` | Max-combinator with disjoint-group trait |
| `tests/tables/test_stage_pdb.cpp` | Sampled admissibility tests |
| `tests/tables/test_move_pruning.cpp` | Move-pruning correctness |
| `tests/core/test_heuristic.cpp` | Combinator logic tests |

### 6.3 Solver Integration Pattern (Read-Only for This Agent)

Solvers will (in Wave 3) do:
```cpp
// In solver.cpp, at init:
static const tables::StageConfig CROSS_CONFIG = {
    .stage_name = "cross",
    .patterns = {{"cross18", {4,5,6,7}, true, 190080}},
    .disjoint_groups = {}, // Single PDB
    .allowed_moves = all_18_face_move_indices(),
    .goal_predicate = is_cross_solved,
    .goal_states = {CubeState::solved()} // Cross solved = D-edges in place
};
auto cross_pdbs = tables::buildStagePdbs(CROSS_CONFIG);
auto cross_heuristic = core::makeCombinedHeuristic(cross_pdbs, CROSS_CONFIG.disjoint_groups);
```

**This agent does not modify solver code** — only provides the shared infra they consume.

---

## 7. Acceptance Checks

- [ ] `DESIGN.md` committed with per-stage admissibility, table sizes, build budget
- [ ] Generic stage-PDB builder: backward BFS from stage goal using stage move set
- [ ] Max-combinator heuristic: `max(h₁, h₂, ...)` for overlapping; `h₁ + h₂` for disjoint (trait-based)
- [ ] Move-pruning table per stage move set (opposite faces, same face)
- [ ] Root-split parallelism in `IdaSearcher` with early abort via atomic flag
- [ ] Extension point: solvers register `StageConfig` (patterns, move set, goal, disjoint groups)
- [ ] Sampled admissibility test for each new PDB (h ≤ true stage distance, verified against search without PDB)
- [ ] `tests/tables/hashes.txt` entries for all new tables
- [ ] `ctest` passes
- [ ] `git diff korf-base -- src/solvers/twophase src/core/interface.hpp tests/core/reference_sim.hpp` is empty

---

## 8. Stop Conditions

| Condition | Action |
|-----------|--------|
| Admissibility proof gap | Write BLOCKED report, stop |
| Any table size > 1 GB | Write BLOCKED report, stop and ask |
| Any build step > 15 min | Write BLOCKED report, stop and ask |
| Any test fails | Diagnose and fix (max 3 attempts per failing step) |

---

*End of DESIGN.md*