#pragma once
// Optimal solver: IDA* with heuristic = max of 4 data PDBs PLUS tables
// HTM heuristics (cornerHTM, edgePermHTM, phase1HTM). See docs/SPEC.md
// Section 8 + docs/tables.md Section 4 (approved loader, hash 34/34,
// depth-1 0/18, depth<=6 0/1000).
//
// Following Korf (1997), "Finding Optimal Solutions to Rubik's Cube Using
// Pattern Databases" (AAAI/IAAI): each PDB abstracts the cube to a subset
// of cubies, BFS from the solved abstract state yields exact pattern
// distances, and max of PDB values is an admissible heuristic for IDA*.
//
// Layout inspiration only from crux (https://github.com/aravinth-kanesh/crux);
// no code copied. All move tables, ranking functions, BFS and search below
// are original implementations on top of the frozen core cubie model
// (src/core/cube.hpp + moves.hpp).
//
// PDBs (4-bit packed entries, see solver.cpp for file format):
//   corner : all 8 corners, 8! * 3^7 = 88,179,840 entries
//   edge1  : cubies UR,UF,UL,UB,DR,DF, P(12,6)*2^6 = 42,577,920 entries
//   edge2  : cubies DL,DB,FR,FL,BL,BR, 42,577,920 entries
//   edge3  : cubies UR,UL,DR,DL,FR,BL, 42,577,920 entries
// Packed at 4 bits/entry this is ~44.1 MB + 3 * ~21.3 MB ~= 108 MB on disk
// in data/ (never committed). Distances are capped at 14 with 15 reserved
// as the unvisited sentinel during BFS; a stored 15 therefore never
// overestimates (it is <= the true distance), so the heuristic stays
// admissible even if some pattern were farther than 14.
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/interface.hpp"

namespace rubik::optimal {

// PDB constants shared by solver, builder (src/build_pattern_db.cpp) and
// tests so file format, sizes and index definitions cannot drift apart.
inline constexpr std::uint32_t kPdbVersion = 1;
inline constexpr std::uint32_t kCornerEntries = 88179840u;  // 40320 * 2187
inline constexpr std::uint32_t kEdgeEntries = 42577920u;    // 665280 * 64
inline constexpr std::uint32_t kCornerPermEntries = 40320u;    // 8!
inline constexpr std::uint32_t kCornerOrientEntries = 2187u;  // 3^7
inline constexpr std::uint32_t kEdgeRankEntries = 665280u;    // P(12,6)
inline constexpr std::uint32_t kPdbIdCorner = 0;
inline constexpr std::uint32_t kPdbIdEdge1 = 1;
inline constexpr std::uint32_t kPdbIdEdge2 = 2;
inline constexpr std::uint32_t kPdbIdEdge3 = 3;
inline constexpr const char* kCornerFileName = "corner.db";
inline constexpr const char* kEdge1FileName = "edge1.db";
inline constexpr const char* kEdge2FileName = "edge2.db";
inline constexpr const char* kEdge3FileName = "edge3.db";

// Distinguished edge cubie IDs (EP order UR,UF,UL,UB,DR,DF,DL,DB,FR,FL,BL,BR
// = 0..11) for edge PDBs 0..2 respectively.
inline constexpr int kEdgeCubies[3][6] = {
    {0, 1, 2, 3, 4, 5},     // UR,UF,UL,UB,DR,DF
    {6, 7, 8, 9, 10, 11},   // DL,DB,FR,FL,BL,BR
    {0, 2, 4, 6, 8, 10},    // UR,UL,DR,DL,FR,BL
};

// Pattern indices. Index layouts:
//   corner = permRank(cp) * 2187 + orientIndex(co[0..6] base-3)
//   edge   = rank(positions of distinguished cubies) * 64 + orientBits
// where orientBits bit d is EO at the position of kEdgeCubies[pdb][d].
// These are bijections onto [0, entries); see solver.cpp.
// Solved maps to 0 for the corner PDB. For edge PDBs the solved index
// is generally non-zero (e.g. cubies 6..11 sit at positions 6..11, whose
// rank is not 0), so BFS and tests must start from
// solved_pattern_index(), where the stored PDB value is 0.
std::uint32_t corner_index(const CubeState& state);
std::uint32_t edge_index(const CubeState& state, int edge_pdb);
// Pattern index of the solved state for pdb_id 0..3 (corner, edge1..3).
std::uint32_t solved_pattern_index(std::uint32_t pdb_id);

// O(1) successors in pattern space via precomputed 18-move tables.
// move is 0..17 in rubik::all_face_moves() order
// (U,U',U2,R,R',R2,F,F',F2,D,D',D2,L,L',L2,B,B',B2).
// Out-of-range move returns the input index unchanged.
std::uint32_t corner_successor(std::uint32_t index, int move);
std::uint32_t edge_successor(std::uint32_t index, int edge_pdb, int move);

// 4-bit packed PDB access: entry idx lives in byte idx/2, low nibble when
// idx is even, high nibble when odd (builder and solver agree).
inline std::uint8_t pdb_get(const std::vector<std::uint8_t>& packed,
                            std::uint32_t idx) {
  const std::uint8_t byte = packed[idx / 2u];
  return (idx % 2u == 0u) ? static_cast<std::uint8_t>(byte & 0x0Fu)
                          : static_cast<std::uint8_t>(byte >> 4);
}

// Load one PDB file and validate version header + exact size.
// Returns packed entries (without header). Any missing file, bad magic,
// version/id/entry-count mismatch or size mismatch yields
// DatabaseMissing; callers must not search with a partial database.
std::expected<std::vector<std::uint8_t>, SolveError> load_pdb_file(
    const std::filesystem::path& path, std::uint32_t expected_id,
    std::uint32_t expected_entries);

// Data-directory resolution for solve(). Precedence: explicit override via
// set_data_dir(), else $RUBIK_DATA_DIR, else first of ./data, ../data
// containing corner.db, else ./data. An explicit override/env is used
// exactly (no fallback), so tests can force DatabaseMissing with an
// empty directory. Passing an empty path clears the override.
void set_data_dir(const std::filesystem::path& dir);
std::filesystem::path resolve_data_dir();

// tables\ directory forwarding (uses rubik::tables adapter + mmap loader).
// Default is .\\tables via rubik::tables::default_tables_dir(). When the
// tables\\ HTM set is missing/invalid, solve() prints the adapter
// status_message and falls back to the data\\ PDBs (still admissible).
void set_tables_dir(const std::filesystem::path& dir);
std::filesystem::path resolve_tables_dir();
std::filesystem::path default_tables_dir();

// BFS-build one PDB from the solved pattern over the 18 face
// moves and write it (header + packed entries) to out_path. Used by the
// build_pattern_db executable; progress/distance summary goes to
// std::cout. Returns an error message on failure.
std::expected<void, std::string> build_and_write_pdb(
    std::uint32_t pdb_id, const std::filesystem::path& out_path);

// Optimal IDA* solver. Returns a single-stage Solution ("optimal") whose
// moves solve the cube, only when IDA* completed (hence optimal by
// admissibility of the PDB heuristic). Timeout -> Timeout, external
// stop_token -> Cancelled, missing/mismatched DB -> DatabaseMissing.
// Exceeding max_depth without completing -> Timeout (never a
// suboptimal solution reported as optimal). Unsolvable/invalid cubie
// state -> Unsolvable.
std::expected<Solution, SolveError> solve(const CubeState& state,
                                          const SolveOptions& options);

}  // namespace rubik::optimal
