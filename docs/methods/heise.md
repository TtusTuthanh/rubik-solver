# Heise Method — Canonical Stages

Sources (accessed 2026-11-08, P0 Recon researcher report ses_e46c4366dffeOUPXTP6hJRBcSN):

- SpeedSolving Wiki: https://www.speedsolving.com/wiki/Heise_method
- Ryan Heise (proposer): http://www.ryanheise.com/cube/heise_method.html

Both sources agree on the canonical 4-step sequence (SpeedSolving Wiki infobox: proposer Ryan Heise, 2003, 4 steps, 0 algorithms).

Variants listed on the wiki ("Old Heise", "Speed-Heise", "FLFL") are NOT the canonical method and are out of scope.

## Stages

1. **Build four 1x2x2 blocks / squares** — Wiki §Steps: "Create four 1x2x2 blocks (also called Heise blocks), making sure that one color appears on none of the blocks. … One of these blocks should be just two centers and an edge." Ryan Heise: "Build 4 successive square-shaped blocks…" (terminology differs: Heise blocks 1x2x2 vs squares 2x2x1, same piece count; step identity agrees).
2. **Pair / match blocks + orient last-layer edges → F2L-1** — Wiki §2: "Pair up the 1x2x2 blocks, while simultaneously orienting the last-layer edges… all of F2L minus a corner/edge pair finished." Ryan Heise §2: "match up the squares … and orient the remaining edges."
3. **Solve last 5 edges + 2 corners (via two corner-edge pairs)** — Wiki §3: "Create two corner-edge pairs, and then solve all of the edges and two corners." Ryan Heise §3: "solve all of the remaining edges and any two corners."
4. **Solve final 3 corners with commutator** — Wiki §4: "Solve the final three corners with a commutator." Ryan Heise §4: "last three corners … solved using commutators."

## Solver contract (this project)

- Assert the stage goal predicate after each stage.
- Enforce the restricted move set per stage with an assertion.
- Per-stage IDA\*/BFS with small pruning tables allowed; algorithm tables allowed only if simulation-verified in tests.
- Human-style fingertricks NOT required.
