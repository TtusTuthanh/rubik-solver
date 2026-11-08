#pragma once
// Portable loader for upstream `tables\` raw files (docs/tables.md).
//
// Scope: ALL 34 files incl. two-phase, HTM + all metrics. Files are
// headerless raw `fwrite` dumps; this loader never writes into `tables\`
// and never adds headers. All 34 expected sizes and SHA-256 hashes are
// transcribed verbatim from docs/tables.md Section 1.1 (upstream public
// table page; see tables.md Section 6 for origin; licence unknown).
//
// Portability: Win32 MapViewOfFile via <windows.h> guarded by
// `#ifdef _WIN32`, mmap on POSIX, buffered-read fallback. No deps
// beyond C++23 stdlib (+ windows.h on Windows). File size is always
// checked against the expected component bytes BEFORE mapping;
// SHA-256 (implemented inline) fails closed on mismatch when
// verification is requested. Little-endian host is required
// (std::endian check); little-endian integers are read via memcpy
// so unaligned access is never UB.
//
// Encoders implement the upstream ranking (docs/tables.md Section 3.2
// plus exact upstream src/encode constants): cubie-indexed PieceCube,
// shuffled twist weights, 11-bit flip scatter, comb495 x 24, symmetry
// class, phase2 ((class*40320+UD)*24+slice)>>1. Phase-2 symmetric
// positions (getPhase2SymmetricPosition) are computed by
// phase2_corner_sym / phase2_ud_sym / phase2_e_sym and consumed by
// the two-phase solver.

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/cube.hpp"

namespace rubik::tables {

// -------------------------------------------------------------------------
// Metrics (suffix in filenames; movement moveCounts differ per metric).
// HTM=Half-Turn (18 moves, phase2 10), QTM=Quarter-Turn (18, 10),
// STM=Slice-Turn (27, 15), ATM=Axial-Turn (45, 21).
// -------------------------------------------------------------------------
enum class Metric { HTM, QTM, STM, ATM };

constexpr std::string_view metric_suffix(Metric m) noexcept {
  switch (m) {
    case Metric::HTM: return "HTM";
    case Metric::QTM: return "QTM";
    case Metric::STM: return "STM";
    case Metric::ATM: return "ATM";
  }
  return "HTM";
}

// Moves stored per coordinate in movement files (layout [idx*moves+move]).
constexpr std::uint32_t phase1_move_count(Metric m) noexcept {
  switch (m) {
    case Metric::HTM: return 18u;
    case Metric::QTM: return 18u;
    case Metric::STM: return 27u;
    case Metric::ATM: return 45u;
  }
  return 18u;
}
constexpr std::uint32_t phase2_move_count(Metric m) noexcept {
  switch (m) {
    case Metric::HTM: return 10u;
    case Metric::QTM: return 10u;
    case Metric::STM: return 15u;
    case Metric::ATM: return 21u;
  }
  return 10u;
}

// -------------------------------------------------------------------------
// Expected manifest: all 34 files, sizes + SHA-256 hex (lowercase) from
// docs/tables.md Section 1.1. Loader MUST compare file size == expected
// before mapping and (when verify_hashes is on) SHA-256 fail-closed.
// -------------------------------------------------------------------------
struct TableFileSpec {
  std::string_view filename;
  std::uint64_t expected_bytes;
  std::string_view sha256_hex;
};

inline constexpr std::array<TableFileSpec, 34> kTableSpecs = {{
    {"pieceCubeMovementHTM", 1008ull, "1ef2c994a1d42e64e4c423f05618c4f2baa0cb473347b5e5c393f5c1cb0e7b48"},
    {"pieceCubeMovementQTM", 1008ull, "1ef2c994a1d42e64e4c423f05618c4f2baa0cb473347b5e5c393f5c1cb0e7b48"},
    {"pieceCubeMovementSTM", 1512ull, "143d7e6c9cf3cbb6e8a37a167dceec78a75d1892be5dae38811a03501483960a"},
    {"pieceCubeMovementATM", 2520ull, "9e5209a01c7b45e74b7470460418313c8bd3ab3998b8e2f383856b03c5a846d0"},
    {"kociembaPhase1MovementHTM", 2015640ull, "251a48f93106c63787756b3e8b0740ae421cba7f06d1b7ccd6cc9b7805a4138a"},
    {"kociembaPhase1MovementQTM", 2015640ull, "251a48f93106c63787756b3e8b0740ae421cba7f06d1b7ccd6cc9b7805a4138a"},
    {"kociembaPhase1MovementSTM", 3023460ull, "47074151dbb606659993b5ac9675ccfba3b31d38675258c017979a695448604a"},
    {"kociembaPhase1MovementATM", 5039100ull, "34f6290ec1e2b263c77e627124be0ccfba55ff4f84b9d3cff46c79f1352c6cf4"},
    {"kociembaPhase2MovementHTM", 3226560ull, "27cead0b3955d4d3dee7ae7679ea48eb0d03e7b551ff056e593dd88e47ad3abb"},
    {"kociembaPhase2MovementQTM", 3226560ull, "27cead0b3955d4d3dee7ae7679ea48eb0d03e7b551ff056e593dd88e47ad3abb"},
    {"kociembaPhase2MovementSTM", 4839840ull, "0b31b4bea87f547bc29e7bd617b2c650e1dadc375ccf44de693ed2d0a8918b00"},
    {"kociembaPhase2MovementATM", 6775776ull, "c2d4574b43bba33befe697ec162b5fb62af64e448855f017223ee800c17b3264"},
    {"cornerHeuristicHTM", 44089920ull, "72a5584777650038220389627c8f649d86b37b7b1b80d3ec4a070878b68a28cd"},
    {"cornerHeuristicQTM", 44089920ull, "ef1ecb54ed0759aca8f4024b96fdd75d629d3f1b93a7cc96b53af080466086ab"},
    {"cornerHeuristicSTM", 44089920ull, "1bee584c9acf0e2211e4c1fdaa4c2e4d68e440d75dbb8046e9c792e4cbc33f54"},
    {"cornerHeuristicATM", 44089920ull, "ba9f3fdc129819b05373f4828e9c09873b574ad6c891a32c626a9aad75571faf"},
    {"edgePermutationHeuristicHTM", 239500800ull, "5a1cec6ec3cb169886b79f39d7fee157083e71e6a240192b8a6ea49cbbbeaaca"},
    {"edgePermutationHeuristicQTM", 239500800ull, "2e43880bede7b8ec5777e5b539e1382f9fffe1fa4760b246ef7f13b2d8021f42"},
    {"edgePermutationHeuristicSTM", 239500800ull, "7fb8832463460490f7bacd433273047e933b41cb6f9210cc6ff260b077c6a2ee"},
    {"edgePermutationHeuristicATM", 239500800ull, "e2d4e6a56053ec6dadabb736f46b2c58b465f70b67b628471b0d89568d70ce43"},
    {"kociembaPhase1HeuristicHTM", 100776960ull, "cff8a49ae535c9fe1f3d590c7a34f4726990cf628a125263724629117360a9cf"},
    {"kociembaPhase1HeuristicQTM", 100776960ull, "a278aa34e65e91b2e8311d927dd430cf2ee6372d67f94327dcdfb5a2191700ae"},
    {"kociembaPhase1HeuristicSTM", 100776960ull, "d32d498fc7e44e8176361320639c188bdaad4519b20353af586d3dd39dfb83f5"},
    {"kociembaPhase1HeuristicATM", 100776960ull, "40d065bfadea442bf7b7337190834e28c5895404fc9c857836fccc33e2da9f3f"},
    {"largeKociembaPhase1HeuristicHTM", 2418647040ull, "f5c36e47f04954310a9830d9a93afda97545e4d9e9f7f96fb37e1393459b590b"},
    {"largeKociembaPhase1HeuristicQTM", 2418647040ull, "fb51a5c8b1c2df1986db332e31c02b09e6066d9af59ce01bf047b3172da9a652"},
    {"largeKociembaPhase1HeuristicSTM", 2418647040ull, "6515beda05c63d1b6b412e7831416097e7ada80cd0f6d48f28eb5a4518c11cba"},
    {"largeKociembaPhase1HeuristicATM", 2418647040ull, "70b1c25f07039140a2e4d1d9e64e768da3801e43d8dc57363d94c7f25b68d3ca"},
    {"kociembaPhase2HeuristicHTM", 334817280ull, "cc713b1d5b241ba5d66fa76b56d0947afc4da283e3979e9161f5c5b3557fcb5b"},
    {"kociembaPhase2HeuristicQTM", 334817280ull, "e2e7b37a00c8c37e3d0f9e4ead689a0b2162c8708940c99b43945e45d760dfce"},
    {"kociembaPhase2HeuristicSTM", 334817280ull, "9da08a785d9c689cd9e6f07a717afadcd3e77a35781da7cc81297953ab6514b8"},
    {"kociembaPhase2HeuristicATM", 334817280ull, "20eb576f27b2d578f93bd5d4e92a51c284b16bf74124ca38384c9d877f47fcd9"},
    {"kociembaPhase1Symmetry", 65783403ull, "a254654c03fab69a8eccc56f56434d5f805a7744d6b6d69a8298ce09f484a6eb"},
    {"kociembaPhase2Symmetry", 5364096ull, "371d753fb475848e4569c20d0ccfb6655a26de8d98cff965ce837d62579b617b"},
}};

const TableFileSpec& find_spec(std::string_view filename);

// Entry counts (entries, not bytes) per heuristic family.
inline constexpr std::uint64_t kCornerEntries = 88179840ull;       // 40320*2187
inline constexpr std::uint64_t kEdgePermEntries = 479001600ull;    // 12!
inline constexpr std::uint64_t kPhase1ReducedEntries = 201553920ull;  // 45*2048*2187
inline constexpr std::uint64_t kPhase1LargeEntries = 4837294080ull;   // *24
inline constexpr std::uint64_t kPhase2Entries = 1339269120ull;     // 2-bit packed

constexpr bool is_little_endian() noexcept {
  return std::endian::native == std::endian::little;
}

// Sentinel values: nibble 0xF = unreached; 2-bit 0b11 = unreached.
inline constexpr std::uint8_t kNibbleSentinel = 0xFu;
inline constexpr std::uint8_t kTwoBitSentinel = 0x3u;

// 4-bit nibble decode: byte idx/2, low nibble even, high nibble odd.
// Caller passes a validated mapped view; idx must be in range.
inline std::uint8_t nibble_get(const std::uint8_t* data, std::uint64_t idx) noexcept {
  const std::uint8_t byte = data[idx >> 1u];
  return (idx & 1u) != 0u ? static_cast<std::uint8_t>(byte >> 4)
                          : static_cast<std::uint8_t>(byte & 0x0Fu);
}

// 2-bit decode: byte idx/4, shift (idx&3)*2, mask 0b11. Values mod 3.
inline std::uint8_t two_bit_get(const std::uint8_t* data, std::uint64_t idx) noexcept {
  const std::uint8_t byte = data[idx >> 2u];
  const unsigned shift = static_cast<unsigned>(idx & 3u) * 2u;
  return static_cast<std::uint8_t>((byte >> shift) & 0x3u);
}

// Admissible mapping: sentinel entries (unreached) map to 0 so max() never
// overestimates. Non-sentinel values pass through.
inline int nibble_heuristic(const std::uint8_t* data, std::uint64_t idx,
                            std::uint64_t entries) noexcept {
  if (data == nullptr || idx >= entries) return 0;
  const std::uint8_t v = nibble_get(data, idx);
  return (v == kNibbleSentinel) ? 0 : static_cast<int>(v);
}
inline int two_bit_heuristic(const std::uint8_t* data, std::uint64_t idx,
                             std::uint64_t entries) noexcept {
  if (data == nullptr || idx >= entries) return 0;
  const std::uint8_t v = two_bit_get(data, idx);
  return (v == kTwoBitSentinel) ? 0 : static_cast<int>(v);
}

// Streamed SHA-256 of a file (lowercase hex, 64 chars). Empty string on
// I/O error. No external crypto dependency.
std::string sha256_file_hex(const std::filesystem::path& path);
bool sha256_file_matches(const std::filesystem::path& path,
                         std::string_view expected_hex);

// -------------------------------------------------------------------------
// MappedFile: portable read-only mapping with buffered fallback.
// open() checks file_size == expected_bytes BEFORE mapping and fails
// closed (returns false, valid()==false) on mismatch. Move-only.
// -------------------------------------------------------------------------
class MappedFile {
 public:
  MappedFile() = default;
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&& other) noexcept;
  MappedFile& operator=(MappedFile&& other) noexcept;

  // Map path read-only. expected_bytes must equal file size exactly.
  // Returns false (fail-closed) if missing, size mismatch, or mapping
  // fails in all backends. Prefer mmap; buffered fallback is used only
  // if mapping is unavailable/fails.
  bool open(const std::filesystem::path& path, std::uint64_t expected_bytes);
  // Force buffered-read backend (for tests / mapping-disabled hosts).
  bool open_buffered(const std::filesystem::path& path, std::uint64_t expected_bytes);

  const std::uint8_t* data() const noexcept { return data_; }
  std::size_t size() const noexcept { return size_; }
  bool valid() const noexcept { return data_ != nullptr; }
  void close() noexcept;

 private:
  bool open_mmap(const std::filesystem::path& path, std::uint64_t expected_bytes);
  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::vector<std::uint8_t> buffer_;
#if defined(_WIN32)
  void* view_ = nullptr;
  void* file_handle_ = nullptr;
  void* mapping_handle_ = nullptr;
#else
  void* mapping_ = nullptr;
  std::size_t map_len_ = 0;
  int fd_ = -1;
  void* file_handle_ = nullptr;  // FILE* kept alive while mapped
#endif
};

// -------------------------------------------------------------------------
// Component layout offsets (little-endian, no header) per docs/tables.md
// Sections 2-3. Byte offsets from start of file; u32/u64 reads go via
// memcpy in accessors. Constants double as asserts for the self-test.
// -------------------------------------------------------------------------
struct Phase1SymmetryLayout {
  static constexpr std::uint64_t corner_orient_bytes = 139968ull;    // u32[2187*16]
  static constexpr std::uint64_t middle_edges_bytes = 760320ull;     // u32[11880*16]
  static constexpr std::uint64_t edge_orient_bytes = 64880640ull;    // u32[1013760*16]
  static constexpr std::uint64_t equiv_bytes = 1980ull;              // u32[495]
  static constexpr std::uint64_t min_orient_bytes = 495ull;          // u8[495]
  static constexpr std::uint64_t middle_edges_off = corner_orient_bytes;
  static constexpr std::uint64_t edge_orient_off =
      middle_edges_off + middle_edges_bytes;
  static constexpr std::uint64_t equiv_off = edge_orient_off + edge_orient_bytes;
  static constexpr std::uint64_t min_orient_off = equiv_off + equiv_bytes;
  static constexpr std::uint64_t total_bytes =
      min_orient_off + min_orient_bytes;  // 65783403
};

struct Phase2SymmetryLayout {
  static constexpr std::uint64_t corner_perm_bytes = 2580480ull;     // u32[40320*16]
  static constexpr std::uint64_t ud_edge_bytes = 2580480ull;         // u32[40320*16]
  static constexpr std::uint64_t e_edge_bytes = 1536ull;             // u32[24*16]
  static constexpr std::uint64_t equiv_bytes = 161280ull;            // u32[40320]
  static constexpr std::uint64_t min_orient_bytes = 40320ull;        // u8[40320]
  static constexpr std::uint64_t ud_edge_off = corner_perm_bytes;
  static constexpr std::uint64_t e_edge_off = ud_edge_off + ud_edge_bytes;
  static constexpr std::uint64_t equiv_off = e_edge_off + e_edge_bytes;
  static constexpr std::uint64_t min_orient_off = equiv_off + equiv_bytes;
  static constexpr std::uint64_t total_bytes =
      min_orient_off + min_orient_bytes;  // 5364096
};

// -------------------------------------------------------------------------
// TableLoader: owns per-file MappedFiles in a tables directory.
// Lazy per-file loading; size always enforced; SHA-256 enforced when
// verify_hashes=true (fail-closed). Exposes raw views + symmetry /
// movement accessors for encoders and the two-phase solver.
// -------------------------------------------------------------------------
class TableLoader {
 public:
  explicit TableLoader(std::filesystem::path dir);
  const std::filesystem::path& dir() const noexcept { return dir_; }

  // Load one file by exact filename. Returns true iff mapped and valid
  // (size matched and, if verify_hashes, hash matched). Re-load is a
  // no-op returning current validity.
  bool load(std::string_view filename, bool verify_hashes);
  bool load(std::string_view filename) { return load(filename, false); }
  // Load all 34 files. Returns number of files successfully loaded.
  std::size_t load_all(bool verify_hashes);
  std::size_t load_all() { return load_all(false); }

  const std::uint8_t* view(std::string_view filename) const noexcept;
  std::size_t view_size(std::string_view filename) const noexcept;
  bool is_loaded(std::string_view filename) const noexcept;

  // Fast hot-path accessors by kTableSpecs index (no filename scans).
  // Used by the per-node heuristic path.
  static int index_of(std::string_view filename) noexcept;
  const std::uint8_t* view_indexed(std::size_t idx) const noexcept;
  bool load_indexed(std::size_t idx, bool verify_hashes);
  bool is_loaded_indexed(std::size_t idx) const noexcept;

  // Heuristic getters (return 0 when not loaded / out of range, so
  // max() callers stay admissible; use is_loaded to detect absence).
  int corner_heuristic(std::string_view filename, std::uint64_t idx) const noexcept;
  int phase2_heuristic(std::string_view filename, std::uint64_t idx) const noexcept;

  // Symmetry accessors require the symmetry file to be loaded; they
  // return 0 on missing/out-of-range (documented, admissible-safe).
  const std::uint8_t* phase1_symmetry_base() const noexcept;
  const std::uint8_t* phase2_symmetry_base() const noexcept;
  std::uint32_t phase1_corner_sym(std::uint32_t twist, std::uint32_t sym) const noexcept;
  std::uint32_t phase1_middle_sym(std::uint32_t middle_layer, std::uint32_t sym) const noexcept;
  std::uint32_t phase1_edge_sym(std::uint32_t flip, std::uint32_t comb495,
                                std::uint32_t sym) const noexcept;
  std::uint32_t phase1_equiv_class(std::uint32_t middle_sym) const noexcept;
  std::uint8_t phase1_min_orientation(std::uint32_t comb495) const noexcept;
  std::uint32_t phase2_equiv_class(std::uint32_t sym_corner_perm) const noexcept;
  std::uint8_t phase2_min_orientation(std::uint32_t corner_perm) const noexcept;
  std::uint32_t phase2_corner_sym(std::uint32_t corner_perm, std::uint32_t sym) const noexcept;
  std::uint32_t phase2_ud_sym(std::uint32_t ud_perm, std::uint32_t sym) const noexcept;
  std::uint32_t phase2_e_sym(std::uint32_t slice_perm, std::uint32_t sym) const noexcept;

  // Movement successors: layout [coord*moveCount + move]. Out-of-range
  // coord/move (or unloaded file) returns coord unchanged (mirrors
  // the frozen HTM solver's successor semantics).
  std::uint32_t phase1_corner_move(std::uint32_t coord, std::uint32_t move,
                                   Metric metric) const noexcept;
  std::uint32_t phase1_edge_move(std::uint32_t coord, std::uint32_t move,
                                 Metric metric) const noexcept;
  std::uint64_t phase1_middle_move(std::uint64_t coord, std::uint32_t move,
                                   Metric metric) const noexcept;
  std::uint32_t phase2_corner_move(std::uint32_t coord, std::uint32_t move,
                                   Metric metric) const noexcept;
  std::uint32_t phase2_ud_move(std::uint32_t coord, std::uint32_t move,
                               Metric metric) const noexcept;
  std::uint32_t phase2_e_move(std::uint32_t coord, std::uint32_t move,
                              Metric metric) const noexcept;
  std::uint8_t piece_cube_corner_move(std::uint32_t piece_cube, std::uint32_t move,
                                      Metric metric) const noexcept;
  std::uint8_t piece_cube_edge_move(std::uint32_t piece_cube, std::uint32_t move,
                                    Metric metric) const noexcept;

 private:
  std::filesystem::path dir_;
  std::vector<std::unique_ptr<MappedFile>> maps_;  // index = kTableSpecs order
  std::vector<bool> loaded_;
  std::vector<bool> has_view_;
};

// -------------------------------------------------------------------------
// Upstream encoders (docs/tables.md Section 3.2). All take frozen-core
// CubeState and return upstream-ranking indexes (bijection onto the
// file domain, solved maps to 0 where applicable).
// -------------------------------------------------------------------------

// Upstream PieceCube facts (numeric constants transcribed from the
// upstream public solver, James Cameron; licence unknown,
// no LICENSE in upstream repo; attribution only, no code copied):
// upstream PieceCube is CUBIE-indexed: corners[cubie]=(position<<2)|twist,
// edges[cubie]=(position<<1)|flip, with position/cubie ids in the lists
//   corners: UBL,UFL,UBR,UFR,DBL,DFL,DBR,DFR (common.h UBL_CORNER..DFR_CORNER)
//   edges:   UB,UR,UF,UL,FL,FR,BR,BL,DB,DL,DF,DR (common.h UB_EDGE..DR_EDGE)
// orderCornerPermutations=[2,0,6,4,3,1,7,5],
// unorderCornerPermutations=[1,5,0,4,3,7,2,6] (encodeCornerState.c).
// Upstream orientation equals frozen-core CO and upstream flip equals
// frozen-core EO (verified from upstream sticker/face-value definitions:
// UD<FB<LR face values, primary colour is the smaller; see .cpp).
// unorder is retained ONLY for upstream_unordered_corners() inspection;
// heuristics rank the cubie-indexed PieceCube directly (ranking the
// position-indexed inverse permutation is the prior BFS failure).
inline constexpr std::array<std::uint8_t, 8> kUpstreamUnorder = {1, 5, 0, 4, 3, 7, 2, 6};
inline constexpr std::array<std::uint8_t, 8> kUpstreamOrder = {2, 0, 6, 4, 3, 1, 7, 5};

// Upstream position id (in UBL..DFR list) -> frozen-core CP position:
// UBL=ULB=2, UFL=1, UBR=3, UFR=URF=0, DBL=6, DFL=DLF=5, DBR=DRB=7, DFR=4.
inline constexpr std::array<std::uint8_t, 8> kUpstreamCornerPos = {2, 1, 3, 0, 6, 5, 7, 4};
// Core cubie id -> upstream cubie id (same name correspondence).
inline constexpr std::array<std::uint8_t, 8> kCoreToUpstreamCorner = {3, 1, 0, 2, 7, 5, 4, 6};
// Upstream edge position id (UB..DR list) -> frozen-core EP position.
inline constexpr std::array<std::uint8_t, 12> kUpstreamEdgePos = {3, 0, 1, 2, 9, 8, 11, 10, 7, 6, 5, 4};
// Core cubie id -> upstream cubie id.
inline constexpr std::array<std::uint8_t, 12> kCoreToUpstreamEdge = {1, 2, 3, 0, 11, 10, 9, 8, 5, 4, 7, 6};

// Shuffled twist weights, upstream cornerValues (encodeKociembaPhase1.c):
// index by (position<<2)|twist; value = twist * place[position] with
// place = [0,729,243,81,27,9,3,1] (position 0 is the dependent twist).
// Numeric values transcribed from upstream (James Cameron, licence unknown).
inline constexpr std::array<std::uint32_t, 32> kCornerOrientWeights = {
    0, 0, 0, 0, 0, 729, 1458, 2187, 0, 243, 486, 729, 0, 81, 162, 243,
    0, 27, 54, 81, 0, 9, 18, 27, 0, 3, 6, 9, 0, 1, 2, 3};

// 11-bit flip, upstream edgeTableValues (encodeKociembaPhase1.c):
// packed=(position<<1)|flip (position in UB..DR list, cubie-indexed
// PieceCube). Even packed is 0; odd packed maps position p to bit
// (11-p): p=1->bit10, p=2->bit9, ..., p=11->bit0. Position 0 (UB)
// is the dependent edge (odd packed 0). Solved (all flips 0) maps
// to 0. Numeric constants transcribed from upstream (James Cameron,
// licence unknown); values only, no code copied.
inline constexpr std::array<std::uint32_t, 24> kEdgeOrientValues = {
    0, 0, 0, 1u << 10, 0, 1u << 9, 0, 1u << 8, 0, 1u << 7, 0, 1u << 6,
    0, 1u << 5, 0, 1u << 4, 0, 1u << 3, 0, 1u << 2, 0, 1u << 1, 0, 1u << 0};

// Middle-slice edges in upstream order: FL,FR,BR,BL (positions 4,5,6,7).
inline constexpr std::array<std::uint8_t, 4> kSliceUpstreamPos = {4, 5, 6, 7};
inline constexpr std::uint32_t kCombEntries = 495u;   // C(12,4)
inline constexpr std::uint32_t kSlicePermEntries = 24u;  // 4!
inline constexpr std::uint32_t kMiddleEntries = 11880u;  // 495*24

// Corner coordinate: encodeAllCorners (perm*2187 + orientIndex with
// digit order cubies 5,6,7,0,1,2,3) on fused upstream cubies.
// Domain 88,179,840. Solved maps to 0.
std::uint64_t upstream_corner_index(const CubeState& state);
// Raw upstream PieceCube order after an EXTRA unorder gather
// (double-gather; for pieceCube/movement-table inspection only).
std::array<std::uint8_t, 8> upstream_unordered_corners(const CubeState& state);
// Edge-permutation coordinate: 12! rank on 12 edges, orientation
// stripped. Domain 479,001,600. Solved maps to 0.
std::uint64_t upstream_edge_perm_index(const CubeState& state);
// Phase-1 bundle (single to_piece_cube pass). The per-node heuristic
// path uses this instead of calling the individual encoders (each of
// which rebuilds the PieceCube): correctness-neutral, ~5x fewer
// conversions per heuristic evaluation.
// middle == comb495 * 24 + perm24 (same as upstream_middle_layer).
struct Phase1Coords {
  std::uint32_t twist = 0;   // 0..2186
  std::uint32_t flip = 0;    // 11-bit
  std::uint32_t comb495 = 0;
  std::uint32_t perm24 = 0;
  std::uint32_t middle = 0;
};
void upstream_phase1_coords(const CubeState& state, Phase1Coords& out) noexcept;

// Phase-1 coordinates (domains: twist 2187, flip 2048, comb 495,
// perm 24, middle 11880 = comb*24+perm).
std::uint32_t upstream_twist(const CubeState& state);
std::uint32_t upstream_flip(const CubeState& state);
std::uint32_t upstream_comb495(const CubeState& state);
std::uint32_t upstream_perm24(const CubeState& state);
std::uint32_t upstream_middle_layer(const CubeState& state);
// Phase-2 bundle (single to_piece_cube pass). Phase-2 distance
// heuristics evaluate per search node; one pass instead of three.
struct Phase2Coords {
  std::uint32_t corner_perm = 0;  // 40320
  std::uint32_t ud_perm = 0;      // 40320
  std::uint32_t slice_perm = 0;   // 24
};
void upstream_phase2_coords(const CubeState& state, Phase2Coords& out) noexcept;

// Phase-2 coordinates (domains 40320 / 40320 / 24).
std::uint32_t upstream_corner_perm(const CubeState& state);
std::uint32_t upstream_ud_perm(const CubeState& state);
std::uint32_t upstream_slice_perm(const CubeState& state);

// Reduced Phase-1 index: ((class45*2048)+flipSym)*2187+twistSym.
// Large index: reduced*24 + (midSym%24).
std::uint64_t phase1_reduced_index(std::uint32_t class45, std::uint32_t flip_sym,
                                   std::uint32_t twist_sym);
std::uint64_t phase1_large_index(std::uint32_t class45, std::uint32_t flip_sym,
                                 std::uint32_t twist_sym, std::uint32_t mid_sym);
// Phase-2 index: ((class2768*40320)+UDperm)*24+slicePerm) >> 1
// (parity halving).
std::uint64_t phase2_index(std::uint32_t class2768, std::uint32_t ud_perm,
                           std::uint32_t slice_perm);

// -------------------------------------------------------------------------
// Directory resolution / fallback (CLI plumbing). The integrator wires
// --tables-dir in src/main.cpp and src/visualiser.cpp; this module only
// exposes the API (it must NOT edit those files).
// Default is ".\tables" (tables) per task scope.
// -------------------------------------------------------------------------
std::filesystem::path default_tables_dir();
void set_tables_dir(const std::filesystem::path& dir);
std::filesystem::path resolve_tables_dir();
std::filesystem::path resolve_tables_dir(const std::filesystem::path& explicit_dir);
// Scan argv for `--tables-dir <path>` (integrator helper). Returns true
// and sets out when found; the value is used exactly (no fallback).
bool extract_tables_dir_flag(int argc, char* const argv[], std::filesystem::path& out);

struct LoadStatus {
  bool ok = false;               // tables directory present and validated
  bool using_tables = false;     // true when tables heuristics are live
  bool fell_back_to_data = false;  // true when tables invalid/missing
  std::string message;           // human-readable verdict for logs/CLI
};

// Classify a directory without loading views: checks presence + size of
// the HTM heuristic set (corner, edgePerm, phase1, phase2 + both
// symmetry files) as the minimal usable set. Full 34-file load_all()
// is stricter; adapter reports fallback if its chosen set fails.
LoadStatus describe_tables_dir(const std::filesystem::path& dir, bool verify_hashes);

}  // namespace rubik::tables
