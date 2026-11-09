#include "tables/tables_loader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace rubik::tables {

// -------------------------------------------------------------------------
// Spec lookup (linear over 34 entries; called rarely at load time).
// -------------------------------------------------------------------------
const TableFileSpec& find_spec(std::string_view filename) {
  for (const auto& s : kTableSpecs) {
    if (s.filename == filename) return s;
  }
  // Valid filename by construction elsewhere; match first entry as a
  // safe fallback so callers never receive a dangling reference.
  return kTableSpecs[0];
}

namespace {

int spec_index(std::string_view filename) noexcept {
  for (std::size_t i = 0; i < kTableSpecs.size(); ++i) {
    if (kTableSpecs[i].filename == filename) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::string phase1_move_name(Metric m) {
  return "kociembaPhase1Movement" + std::string(metric_suffix(m));
}
std::string phase2_move_name(Metric m) {
  return "kociembaPhase2Movement" + std::string(metric_suffix(m));
}
std::string piece_cube_move_name(Metric m) {
  return "pieceCubeMovement" + std::string(metric_suffix(m));
}

// Little-endian reads via memcpy (host is little-endian checked at load;
// memcpy avoids unaligned-access UB and alignment assumptions).
std::uint32_t read_u32(const std::uint8_t* base, std::uint64_t byte_off) noexcept {
  std::uint32_t v = 0;
  std::memcpy(&v, base + byte_off, sizeof(v));
  return v;
}
std::uint64_t read_u64(const std::uint8_t* base, std::uint64_t byte_off) noexcept {
  std::uint64_t v = 0;
  std::memcpy(&v, base + byte_off, sizeof(v));
  return v;
}

// -------------------------------------------------------------------------
// SHA-256 (inline, no deps). Standard FIPS 180-4.
// -------------------------------------------------------------------------
constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline std::uint32_t sha256_rotr(std::uint32_t x, unsigned n) noexcept {
  return (x >> n) | (x << (32u - n));
}

struct Sha256 {
  std::array<std::uint32_t, 8> h = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                    0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                    0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, 64> block{};
  std::size_t block_len = 0;
  std::uint64_t total_len = 0;

  void compress(const std::uint8_t* data) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(data[4 * i + 0]) << 24u) |
             (static_cast<std::uint32_t>(data[4 * i + 1]) << 16u) |
             (static_cast<std::uint32_t>(data[4 * i + 2]) << 8u) |
             static_cast<std::uint32_t>(data[4 * i + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^
                               (w[i - 15] >> 3);
      const std::uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^
                               (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = hh + s1 + ch + kSha256K[i] + w[i];
      const std::uint32_t s0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  void update(const std::uint8_t* data, std::size_t len) noexcept {
    total_len += static_cast<std::uint64_t>(len);
    while (len > 0) {
      const std::size_t take = block_len + len < 64 ? len : (64 - block_len);
      std::memcpy(block.data() + block_len, data, take);
      block_len += take;
      data += take;
      len -= take;
      if (block_len == 64) {
        compress(block.data());
        block_len = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> final() noexcept {
    const std::uint64_t bit_len = total_len * 8u;
    std::uint8_t pad = 0x80u;
    update(&pad, 1);
    const std::uint8_t zero = 0;
    while (block_len != 56) update(&zero, 1);
    std::array<std::uint8_t, 8> lenb{};
    for (int i = 0; i < 8; ++i) {
      lenb[static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>(bit_len >> (56 - 8 * i));
    }
    // Feed length without disturbing total_len accounting.
    for (std::size_t i = 0; i < 8; ++i) {
      block[block_len++] = lenb[i];
      if (block_len == 64) {
        compress(block.data());
        block_len = 0;
      }
    }
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; ++i) {
      out[4 * i + 0] = static_cast<std::uint8_t>(h[i] >> 24);
      out[4 * i + 1] = static_cast<std::uint8_t>(h[i] >> 16);
      out[4 * i + 2] = static_cast<std::uint8_t>(h[i] >> 8);
      out[4 * i + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return out;
  }
};

std::string to_hex(const std::array<std::uint8_t, 32>& digest, std::size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back(kHex[digest[i] >> 4]);
    out.push_back(kHex[digest[i] & 0xFu]);
  }
  return out;
}

// -------------------------------------------------------------------------
// Ranking helpers.
// -------------------------------------------------------------------------
constexpr std::array<std::uint32_t, 13> kFact = {
    1u, 1u, 2u, 6u, 24u, 120u, 720u, 5040u, 40320u, 362880u, 3628800u,
    39916800u, 479001600u};

std::uint32_t perm_rank_8(const std::array<std::uint8_t, 8>& a) noexcept {
  std::uint32_t rank = 0;
  bool used[8] = {};
  for (int i = 0; i < 8; ++i) {
    const int c = a[static_cast<std::size_t>(i)];
    int cnt = 0;
    for (int v = 0; v < c; ++v) {
      if (!used[v]) ++cnt;
    }
    rank += static_cast<std::uint32_t>(cnt) *
            kFact[static_cast<std::size_t>(7 - i)];
    used[c] = true;
  }
  return rank;
}

std::uint64_t perm_rank_12(const std::array<std::uint8_t, 12>& a) noexcept {
  std::uint64_t rank = 0;
  bool used[12] = {};
  for (int i = 0; i < 12; ++i) {
    const int c = a[static_cast<std::size_t>(i)];
    int cnt = 0;
    for (int v = 0; v < c; ++v) {
      if (!used[v]) ++cnt;
    }
    rank += static_cast<std::uint64_t>(cnt) *
            static_cast<std::uint64_t>(kFact[static_cast<std::size_t>(11 - i)]);
    used[c] = true;
  }
  return rank;
}

std::uint32_t perm_rank_4(const std::array<std::uint8_t, 4>& a) noexcept {
  std::uint32_t rank = 0;
  bool used[4] = {};
  for (int i = 0; i < 4; ++i) {
    const int c = a[static_cast<std::size_t>(i)];
    int cnt = 0;
    for (int v = 0; v < c; ++v) {
      if (!used[v]) ++cnt;
    }
    rank += static_cast<std::uint32_t>(cnt) *
            kFact[static_cast<std::size_t>(3 - i)];
    used[c] = true;
  }
  return rank;
}

// C(n,k) for n<12 via Pascal table (exact for comb495 edge cases).
std::uint32_t binom_small(int n, int k) noexcept {
  static constexpr std::uint32_t table[13][13] = {
      {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {1, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {1, 4, 6, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0},
      {1, 5, 10, 10, 5, 1, 0, 0, 0, 0, 0, 0, 0},
      {1, 6, 15, 20, 15, 6, 1, 0, 0, 0, 0, 0, 0},
      {1, 7, 21, 35, 35, 21, 7, 1, 0, 0, 0, 0, 0},
      {1, 8, 28, 56, 70, 56, 28, 8, 1, 0, 0, 0, 0},
      {1, 9, 36, 84, 126, 126, 84, 36, 9, 1, 0, 0, 0},
      {1, 10, 45, 120, 210, 252, 210, 120, 45, 10, 1, 0, 0},
      {1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1, 0},
      {1, 12, 66, 220, 495, 792, 924, 792, 495, 220, 66, 12, 1},
  };
  if (k < 0 || k > n || n < 0 || n > 12) return 0;
  return table[n][k];
}

// Position-indexed upstream arrays (kept for upstream_unordered_corners
// inspection only): up_cp[p] = cubie at upstream position p.
void to_upstream_corners(const CubeState& state,
                         std::array<std::uint8_t, 8>& up_cp,
                         std::array<std::uint8_t, 8>& up_co,
                         std::array<std::uint8_t, 8>& co_by_cubie) noexcept {
  for (int p = 0; p < 8; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    const std::size_t core_pos = kUpstreamCornerPos[pp];
    up_cp[pp] = kCoreToUpstreamCorner[state.cp[core_pos]];
    up_co[pp] = state.co[core_pos];
  }
  co_by_cubie.fill(0);
  for (int p = 0; p < 8; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    co_by_cubie[up_cp[pp]] = up_co[pp];
  }
}

[[maybe_unused]] void to_upstream_edges(const CubeState& state,
                       std::array<std::uint8_t, 12>& up_ep,
                       std::array<std::uint8_t, 12>& up_eo) noexcept {
  for (int p = 0; p < 12; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    const std::size_t core_pos = kUpstreamEdgePos[pp];
    up_ep[pp] = kCoreToUpstreamEdge[state.ep[core_pos]];
    up_eo[pp] = state.eo[core_pos];
  }
}

// Upstream PieceCube (cubie-indexed, James Cameron encodeCornerState.c /
// encodeEdgeState.c facts): pc_corners[cubie]=(position<<2)|twist,
// pc_edges[cubie]=(position<<1)|flip, ids in UBL..DFR / UB..DR lists.
// Upstream twist == core CO and upstream flip == core EO (face-value
// ordering UD<FB<LR makes primary-on-primary equivalent to flip==0;
// see header). All upstream encoders rank THESE arrays; ranking the
// position-indexed inverse was the BFS failure (successors disjoint).
void to_piece_cube(const CubeState& state,
                   std::array<std::uint8_t, 8>& pc_corners,
                   std::array<std::uint8_t, 12>& pc_edges) noexcept {
  for (int p = 0; p < 8; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    const std::size_t core_pos = kUpstreamCornerPos[pp];
    const std::size_t cubie = kCoreToUpstreamCorner[state.cp[core_pos]];
    pc_corners[cubie] =
        static_cast<std::uint8_t>((p << 2) | state.co[core_pos]);
  }
  for (int p = 0; p < 12; ++p) {
    const std::size_t pp = static_cast<std::size_t>(p);
    const std::size_t core_pos = kUpstreamEdgePos[pp];
    const std::size_t cubie = kCoreToUpstreamEdge[state.ep[core_pos]];
    pc_edges[cubie] =
        static_cast<std::uint8_t>((p << 1) | state.eo[core_pos]);
  }
}

// Slice-cubie positions (cubies FL,FR,BR,BL = upstream ids 4..7) and
// their relative-order ranks r_i = #{j: q_j < q_i} (upstream
// permutationValues in encodeMiddleLayer.c).
void slice_q_r(const std::array<std::uint8_t, 12>& pc_edges,
               std::array<int, 4>& q, std::array<int, 4>& r) noexcept {
  for (int i = 0; i < 4; ++i) {
    q[static_cast<std::size_t>(i)] =
        pc_edges[static_cast<std::size_t>(4 + i)] >> 1;
    int cnt = 0;
    for (int j = 0; j < 4; ++j) {
      if (pc_edges[static_cast<std::size_t>(4 + j)] >> 1 <
          pc_edges[static_cast<std::size_t>(4 + i)] >> 1) {
        ++cnt;
      }
    }
    r[static_cast<std::size_t>(i)] = cnt;
  }
}

// Directory-resolution state (mirrors the frozen set_data_dir pattern).
std::mutex g_dir_mutex;
std::optional<std::filesystem::path> g_override_dir;

}  // namespace

// -------------------------------------------------------------------------
// SHA-256 public API.
// -------------------------------------------------------------------------
std::string sha256_file_hex(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  Sha256 h;
  std::vector<std::uint8_t> buf(1024u * 1024u);
  while (in) {
    in.read(reinterpret_cast<char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    const std::streamsize got = in.gcount();
    if (got <= 0) break;
    h.update(buf.data(), static_cast<std::size_t>(got));
  }
  if (in.bad()) return {};
  return to_hex(h.final(), 32);
}

bool sha256_file_matches(const std::filesystem::path& path,
                         std::string_view expected_hex) {
  const std::string got = sha256_file_hex(path);
  if (got.empty() || got.size() != expected_hex.size()) return false;
  // Case-insensitive compare (manifest is lowercase).
  for (std::size_t i = 0; i < got.size(); ++i) {
    char a = got[i];
    char b = expected_hex[i];
    if (a >= 'A' && a <= 'F') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'F') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

// -------------------------------------------------------------------------
// MappedFile.
// -------------------------------------------------------------------------
MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(other.data_),
      size_(other.size_),
      buffer_(std::move(other.buffer_))
#if defined(_WIN32)
      ,
      view_(other.view_),
      file_handle_(other.file_handle_),
      mapping_handle_(other.mapping_handle_)
#else
      ,
      mapping_(other.mapping_),
      map_len_(other.map_len_),
      fd_(other.fd_),
      file_handle_(other.file_handle_)
#endif
{
  other.data_ = nullptr;
  other.size_ = 0;
#if defined(_WIN32)
  other.view_ = nullptr;
  other.file_handle_ = nullptr;
  other.mapping_handle_ = nullptr;
#else
  other.mapping_ = nullptr;
  other.map_len_ = 0;
  other.fd_ = -1;
  other.file_handle_ = nullptr;
#endif
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
  if (this == &other) return *this;
  close();
  data_ = other.data_;
  size_ = other.size_;
  buffer_ = std::move(other.buffer_);
#if defined(_WIN32)
  view_ = other.view_;
  file_handle_ = other.file_handle_;
  mapping_handle_ = other.mapping_handle_;
#else
  mapping_ = other.mapping_;
  map_len_ = other.map_len_;
  fd_ = other.fd_;
  file_handle_ = other.file_handle_;
#endif
  other.data_ = nullptr;
  other.size_ = 0;
#if defined(_WIN32)
  other.view_ = nullptr;
  other.file_handle_ = nullptr;
  other.mapping_handle_ = nullptr;
#else
  other.mapping_ = nullptr;
  other.map_len_ = 0;
  other.fd_ = -1;
  other.file_handle_ = nullptr;
#endif
  return *this;
}

void MappedFile::close() noexcept {
#if defined(_WIN32)
  if (view_ != nullptr) {
    ::UnmapViewOfFile(view_);
    view_ = nullptr;
  }
  if (mapping_handle_ != nullptr) {
    ::CloseHandle(static_cast<HANDLE>(mapping_handle_));
    mapping_handle_ = nullptr;
  }
  if (file_handle_ != nullptr) {
    ::CloseHandle(static_cast<HANDLE>(file_handle_));
    file_handle_ = nullptr;
  }
#else
  if (mapping_ != nullptr) {
    ::munmap(mapping_, map_len_);
    mapping_ = nullptr;
    map_len_ = 0;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (file_handle_ != nullptr) {
    std::fclose(static_cast<std::FILE*>(file_handle_));
    file_handle_ = nullptr;
  }
#endif
  data_ = nullptr;
  size_ = 0;
  buffer_.clear();
}

bool MappedFile::open(const std::filesystem::path& path,
                      std::uint64_t expected_bytes) {
  if (!is_little_endian()) return false;
  if (expected_bytes == 0) return false;
  if (expected_bytes > static_cast<std::uint64_t>(
                           (std::numeric_limits<std::size_t>::max)())) {
    return false;
  }
  std::error_code ec;
  const std::uintmax_t fsize = std::filesystem::file_size(path, ec);
  if (ec) return false;
  if (fsize != expected_bytes) return false;  // fail-closed: size before mapping
  return open_mmap(path, expected_bytes);
}

bool MappedFile::open_buffered(const std::filesystem::path& path,
                                std::uint64_t expected_bytes) {
  if (!is_little_endian()) return false;
  if (expected_bytes == 0) return false;
  std::error_code ec;
  const std::uintmax_t fsize = std::filesystem::file_size(path, ec);
  if (ec || fsize != expected_bytes) return false;
  close();
  try {
    buffer_.resize(static_cast<std::size_t>(expected_bytes));
  } catch (...) {
    data_ = nullptr;
    size_ = 0;
    buffer_.clear();
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    buffer_.clear();
    return false;
  }
  in.read(reinterpret_cast<char*>(buffer_.data()),
          static_cast<std::streamsize>(expected_bytes));
  if (!in || static_cast<std::uint64_t>(in.gcount()) != expected_bytes) {
    buffer_.clear();
    data_ = nullptr;
    size_ = 0;
    return false;
  }
  data_ = buffer_.data();
  size_ = static_cast<std::size_t>(expected_bytes);
  return data_ != nullptr;
}

bool MappedFile::open_mmap(const std::filesystem::path& path,
                           std::uint64_t expected_bytes) {
  close();
#if defined(_WIN32)
  const std::wstring wpath = path.wstring();
  HANDLE file = ::CreateFileW(wpath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return open_buffered(path, expected_bytes);
  }
  file_handle_ = static_cast<void*>(file);
  const DWORD high = static_cast<DWORD>(expected_bytes >> 32);
  const DWORD low = static_cast<DWORD>(expected_bytes & 0xffffffffull);
  HANDLE mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, high, low,
                                        nullptr);
  if (mapping == nullptr) {
    close();
    return open_buffered(path, expected_bytes);
  }
  mapping_handle_ = static_cast<void*>(mapping);
  void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0,
                               static_cast<SIZE_T>(expected_bytes));
  if (view == nullptr) {
    close();
    return open_buffered(path, expected_bytes);
  }
  view_ = view;
  data_ = static_cast<const std::uint8_t*>(view);
  size_ = static_cast<std::size_t>(expected_bytes);
  return data_ != nullptr;
#else
  const std::string spath = path.string();
  const int fd = ::open(spath.c_str(), O_RDONLY);
  if (fd < 0) {
    return open_buffered(path, expected_bytes);
  }
  fd_ = fd;
  const std::size_t len = static_cast<std::size_t>(expected_bytes);
  void* mapped = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapped == MAP_FAILED) {
    close();
    return open_buffered(path, expected_bytes);
  }
  mapping_ = mapped;
  map_len_ = len;
  ::close(fd);
  fd_ = -1;  // mapping stays valid after close on POSIX
  data_ = static_cast<const std::uint8_t*>(mapped);
  size_ = len;
  return data_ != nullptr;
#endif
}

// -------------------------------------------------------------------------
// TableLoader.
// -------------------------------------------------------------------------
TableLoader::TableLoader(std::filesystem::path dir)
    : dir_(std::move(dir)),
      maps_(kTableSpecs.size()),
      loaded_(kTableSpecs.size(), false),
      has_view_(kTableSpecs.size(), false) {
  for (std::size_t i = 0; i < maps_.size(); ++i) {
    maps_[i] = std::make_unique<MappedFile>();
  }
}

bool TableLoader::load(std::string_view filename, bool verify_hashes) {
  const int idx = spec_index(filename);
  if (idx < 0) return false;
  const std::size_t i = static_cast<std::size_t>(idx);
  if (loaded_[i]) return has_view_[i];
  loaded_[i] = true;  // remember attempt; has_view_ distinguishes success
  const TableFileSpec& spec = kTableSpecs[i];
  if (!is_little_endian()) return false;
  const std::filesystem::path path = dir_ / std::string(spec.filename);
  std::error_code ec;
  const std::uintmax_t fsize = std::filesystem::file_size(path, ec);
  if (ec || fsize != spec.expected_bytes) {
    has_view_[i] = false;  // fail-closed: never map on size mismatch
    return false;
  }
  if (verify_hashes && !sha256_file_matches(path, spec.sha256_hex)) {
    has_view_[i] = false;
    return false;
  }
  const bool ok = maps_[i]->open(path, spec.expected_bytes);
  has_view_[i] = ok;
  return ok;
}

std::size_t TableLoader::load_all(bool verify_hashes) {
  std::size_t n = 0;
  for (const auto& spec : kTableSpecs) {
    if (load(spec.filename, verify_hashes)) ++n;
  }
  return n;
}

const std::uint8_t* TableLoader::view(std::string_view filename) const noexcept {
  const int idx = spec_index(filename);
  if (idx < 0) return nullptr;
  const std::size_t i = static_cast<std::size_t>(idx);
  if (!has_view_[i] || maps_[i] == nullptr || !maps_[i]->valid()) {
    return nullptr;
  }
  return maps_[i]->data();
}

std::size_t TableLoader::view_size(std::string_view filename) const noexcept {
  const int idx = spec_index(filename);
  if (idx < 0) return 0;
  const std::size_t i = static_cast<std::size_t>(idx);
  if (!has_view_[i] || maps_[i] == nullptr) return 0;
  return maps_[i]->size();
}

bool TableLoader::is_loaded(std::string_view filename) const noexcept {
  return view(filename) != nullptr;
}

int TableLoader::index_of(std::string_view filename) noexcept {
  return spec_index(filename);
}

const std::uint8_t* TableLoader::view_indexed(std::size_t idx) const noexcept {
  if (idx >= maps_.size()) return nullptr;
  if (!has_view_[idx] || maps_[idx] == nullptr || !maps_[idx]->valid()) {
    return nullptr;
  }
  return maps_[idx]->data();
}

bool TableLoader::load_indexed(std::size_t idx, bool verify_hashes) {
  if (idx >= kTableSpecs.size()) return false;
  return load(kTableSpecs[idx].filename, verify_hashes);
}

bool TableLoader::is_loaded_indexed(std::size_t idx) const noexcept {
  return view_indexed(idx) != nullptr;
}

int TableLoader::corner_heuristic(std::string_view filename,
                                  std::uint64_t idx) const noexcept {
  const std::uint8_t* base = view(filename);
  if (base == nullptr) return 0;
  return nibble_heuristic(base, idx, kCornerEntries);
}

int TableLoader::phase2_heuristic(std::string_view filename,
                                  std::uint64_t idx) const noexcept {
  const std::uint8_t* base = view(filename);
  if (base == nullptr) return 0;
  return two_bit_heuristic(base, idx, kPhase2Entries);
}

const std::uint8_t* TableLoader::phase1_symmetry_base() const noexcept {
  return view("kociembaPhase1Symmetry");
}
const std::uint8_t* TableLoader::phase2_symmetry_base() const noexcept {
  return view("kociembaPhase2Symmetry");
}

std::uint32_t TableLoader::phase1_corner_sym(std::uint32_t twist,
                                             std::uint32_t sym) const noexcept {
  const std::uint8_t* base = phase1_symmetry_base();
  if (base == nullptr || twist >= 2187u || sym >= 16u) return 0;
  const std::uint64_t off =
      (static_cast<std::uint64_t>(twist) * 16ull + sym) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase1_middle_sym(std::uint32_t middle_layer,
                                             std::uint32_t sym) const noexcept {
  const std::uint8_t* base = phase1_symmetry_base();
  if (base == nullptr || middle_layer >= 11880u || sym >= 16u) return 0;
  const std::uint64_t off = Phase1SymmetryLayout::middle_edges_off +
                            (static_cast<std::uint64_t>(middle_layer) * 16ull + sym) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase1_edge_sym(std::uint32_t flip,
                                           std::uint32_t comb495,
                                           std::uint32_t sym) const noexcept {
  const std::uint8_t* base = phase1_symmetry_base();
  if (base == nullptr || flip >= 2048u || comb495 >= 495u || sym >= 16u) {
    return 0;
  }
  const std::uint64_t row =
      static_cast<std::uint64_t>(flip) * 495ull + comb495;
  const std::uint64_t off = Phase1SymmetryLayout::edge_orient_off +
                            (row * 16ull + sym) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase1_equiv_class(std::uint32_t middle_sym) const noexcept {
  const std::uint8_t* base = phase1_symmetry_base();
  if (base == nullptr || middle_sym >= 11880u) return 0;
  const std::uint64_t off = Phase1SymmetryLayout::equiv_off +
                            static_cast<std::uint64_t>(middle_sym / 24u) * 4ull;
  return read_u32(base, off);
}
std::uint8_t TableLoader::phase1_min_orientation(std::uint32_t comb495) const noexcept {
  const std::uint8_t* base = phase1_symmetry_base();
  if (base == nullptr || comb495 >= 495u) return 0;
  return base[Phase1SymmetryLayout::min_orient_off + comb495];
}
std::uint32_t TableLoader::phase2_equiv_class(std::uint32_t sym_corner_perm) const noexcept {
  const std::uint8_t* base = phase2_symmetry_base();
  if (base == nullptr || sym_corner_perm >= 40320u) return 0;
  const std::uint64_t off = Phase2SymmetryLayout::equiv_off +
                            static_cast<std::uint64_t>(sym_corner_perm) * 4ull;
  return read_u32(base, off);
}
std::uint8_t TableLoader::phase2_min_orientation(std::uint32_t corner_perm) const noexcept {
  const std::uint8_t* base = phase2_symmetry_base();
  if (base == nullptr || corner_perm >= 40320u) return 0;
  return base[Phase2SymmetryLayout::min_orient_off + corner_perm];
}
std::uint32_t TableLoader::phase2_corner_sym(std::uint32_t corner_perm,
                                             std::uint32_t sym) const noexcept {
  const std::uint8_t* base = phase2_symmetry_base();
  if (base == nullptr || corner_perm >= 40320u || sym >= 16u) return 0;
  const std::uint64_t off =
      (static_cast<std::uint64_t>(corner_perm) * 16ull + sym) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase2_ud_sym(std::uint32_t ud_perm,
                                         std::uint32_t sym) const noexcept {
  const std::uint8_t* base = phase2_symmetry_base();
  if (base == nullptr || ud_perm >= 40320u || sym >= 16u) return 0;
  const std::uint64_t off = Phase2SymmetryLayout::ud_edge_off +
                            (static_cast<std::uint64_t>(ud_perm) * 16ull + sym) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase2_e_sym(std::uint32_t slice_perm,
                                        std::uint32_t sym) const noexcept {
  const std::uint8_t* base = phase2_symmetry_base();
  if (base == nullptr || slice_perm >= 24u || sym >= 16u) return 0;
  const std::uint64_t off = Phase2SymmetryLayout::e_edge_off +
                            (static_cast<std::uint64_t>(slice_perm) * 16ull + sym) * 4ull;
  return read_u32(base, off);
}

std::uint32_t TableLoader::phase1_corner_move(std::uint32_t coord, std::uint32_t move,
                                              Metric metric) const noexcept {
  const std::string name = phase1_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase1_move_count(metric);
  if (base == nullptr || coord >= 2187u || move >= mc) return coord;
  const std::uint64_t off =
      (static_cast<std::uint64_t>(coord) * mc + move) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase1_edge_move(std::uint32_t coord, std::uint32_t move,
                                            Metric metric) const noexcept {
  const std::string name = phase1_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase1_move_count(metric);
  if (base == nullptr || coord >= 2048u || move >= mc) return coord;
  const std::uint64_t comp_off = 2187ull * mc * 4ull;
  const std::uint64_t off = comp_off +
                            (static_cast<std::uint64_t>(coord) * mc + move) * 4ull;
  return read_u32(base, off);
}
std::uint64_t TableLoader::phase1_middle_move(std::uint64_t coord, std::uint32_t move,
                                              Metric metric) const noexcept {
  const std::string name = phase1_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase1_move_count(metric);
  if (base == nullptr || coord >= 11880u || move >= mc) return coord;
  const std::uint64_t comp_off =
      (2187ull + 2048ull) * mc * 4ull;
  const std::uint64_t off = comp_off +
                            (coord * mc + move) * 8ull;
  return read_u64(base, off);
}
std::uint32_t TableLoader::phase2_corner_move(std::uint32_t coord, std::uint32_t move,
                                              Metric metric) const noexcept {
  const std::string name = phase2_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase2_move_count(metric);
  if (base == nullptr || coord >= 40320u || move >= mc) return coord;
  const std::uint64_t off =
      (static_cast<std::uint64_t>(coord) * mc + move) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase2_ud_move(std::uint32_t coord, std::uint32_t move,
                                          Metric metric) const noexcept {
  const std::string name = phase2_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase2_move_count(metric);
  if (base == nullptr || coord >= 40320u || move >= mc) return coord;
  const std::uint64_t comp_off = 40320ull * mc * 4ull;
  const std::uint64_t off = comp_off +
                            (static_cast<std::uint64_t>(coord) * mc + move) * 4ull;
  return read_u32(base, off);
}
std::uint32_t TableLoader::phase2_e_move(std::uint32_t coord, std::uint32_t move,
                                         Metric metric) const noexcept {
  const std::string name = phase2_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase2_move_count(metric);
  if (base == nullptr || coord >= 24u || move >= mc) return coord;
  const std::uint64_t comp_off = 2ull * 40320ull * mc * 4ull;
  const std::uint64_t off = comp_off +
                            (static_cast<std::uint64_t>(coord) * mc + move) * 4ull;
  return read_u32(base, off);
}
std::uint8_t TableLoader::piece_cube_corner_move(std::uint32_t piece_cube,
                                                 std::uint32_t move,
                                                 Metric metric) const noexcept {
  const std::string name = piece_cube_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase1_move_count(metric);
  if (base == nullptr || piece_cube >= 32u || move >= mc) {
    return static_cast<std::uint8_t>(piece_cube & 0xffu);
  }
  return base[static_cast<std::uint64_t>(piece_cube) * mc + move];
}
std::uint8_t TableLoader::piece_cube_edge_move(std::uint32_t piece_cube,
                                               std::uint32_t move,
                                               Metric metric) const noexcept {
  const std::string name = piece_cube_move_name(metric);
  const std::uint8_t* base = view(name);
  const std::uint32_t mc = phase1_move_count(metric);
  if (base == nullptr || piece_cube >= 24u || move >= mc) {
    return static_cast<std::uint8_t>(piece_cube & 0xffu);
  }
  const std::uint64_t comp_off = 32ull * mc;
  return base[comp_off + static_cast<std::uint64_t>(piece_cube) * mc + move];
}

// -------------------------------------------------------------------------
// Encoders.
// -------------------------------------------------------------------------
void upstream_phase1_coords(const CubeState& state, Phase1Coords& out) noexcept {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::uint32_t twist = 0;
  for (int i = 0; i < 8; ++i) {
    twist += kCornerOrientWeights[pc_corners[static_cast<std::size_t>(i)]];
  }
  std::uint32_t flip = 0;
  for (int i = 0; i < 12; ++i) {
    flip |= kEdgeOrientValues[pc_edges[static_cast<std::size_t>(i)]];
  }
  std::array<int, 4> q{};
  std::array<int, 4> r{};
  slice_q_r(pc_edges, q, r);
  std::uint32_t comb = 0;
  for (int i = 0; i < 4; ++i) {
    comb += binom_small(q[static_cast<std::size_t>(i)],
                        r[static_cast<std::size_t>(i)] + 1);
  }
  std::array<std::uint8_t, 4> ru{};
  for (int i = 0; i < 4; ++i) {
    ru[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(r[static_cast<std::size_t>(i)]);
  }
  out.twist = twist;
  out.flip = flip & 0x7ffu;
  out.comb495 = comb;
  out.perm24 = perm_rank_4(ru);
  out.middle = comb * 24u + out.perm24;
}

std::uint32_t upstream_twist(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::uint32_t twist = 0;
  for (int i = 0; i < 8; ++i) {
    twist += kCornerOrientWeights[pc_corners[static_cast<std::size_t>(i)]];
  }
  return twist;  // 0..2186; position 0 dependent (weight 0)
}

std::uint32_t upstream_flip(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::uint32_t flip = 0;
  for (int i = 0; i < 12; ++i) {
    flip |= kEdgeOrientValues[pc_edges[static_cast<std::size_t>(i)]];
  }
  return flip & 0x7ffu;  // 11-bit field (2048 values)
}

std::uint64_t upstream_corner_index(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::array<std::uint8_t, 8> perm{};
  for (int i = 0; i < 8; ++i) {
    perm[static_cast<std::size_t>(i)] =
        pc_corners[static_cast<std::size_t>(i)] >> 2;
  }
  const std::uint32_t rank = perm_rank_8(perm);
  // encodeAllCorners digit order: cubies 5,6,7,0,1,2,3 (cubie-indexed).
  std::uint32_t orient = pc_corners[5] & 3u;
  orient = orient * 3u + (pc_corners[6] & 3u);
  orient = orient * 3u + (pc_corners[7] & 3u);
  orient = orient * 3u + (pc_corners[0] & 3u);
  orient = orient * 3u + (pc_corners[1] & 3u);
  orient = orient * 3u + (pc_corners[2] & 3u);
  orient = orient * 3u + (pc_corners[3] & 3u);
  return static_cast<std::uint64_t>(rank) * 2187ull + orient;
}

std::array<std::uint8_t, 8> upstream_unordered_corners(const CubeState& state) {
  std::array<std::uint8_t, 8> up_cp{};
  std::array<std::uint8_t, 8> up_co{};
  std::array<std::uint8_t, 8> co_by_cubie{};
  to_upstream_corners(state, up_cp, up_co, co_by_cubie);
  (void)up_co;
  (void)co_by_cubie;
  std::array<std::uint8_t, 8> out{};
  for (int i = 0; i < 8; ++i) {
    out[static_cast<std::size_t>(i)] =
        up_cp[kUpstreamUnorder[static_cast<std::size_t>(i)]];
  }
  return out;
}

std::uint64_t upstream_edge_perm_index(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::array<std::uint8_t, 12> perm{};
  for (int i = 0; i < 12; ++i) {
    perm[static_cast<std::size_t>(i)] =
        pc_edges[static_cast<std::size_t>(i)] >> 1;
  }
  return perm_rank_12(perm);  // encodeAllEdges >> 11
}

std::uint32_t upstream_comb495(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::array<int, 4> q{};
  std::array<int, 4> r{};
  slice_q_r(pc_edges, q, r);
  // upstream: sum edgeCombinationValues[r_i][q_i] = C(q_i, r_i+1).
  std::uint32_t comb = 0;
  for (int i = 0; i < 4; ++i) {
    comb += binom_small(q[static_cast<std::size_t>(i)],
                        r[static_cast<std::size_t>(i)] + 1);
  }
  return comb;
}

std::uint32_t upstream_perm24(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::array<int, 4> q{};
  std::array<int, 4> r{};
  slice_q_r(pc_edges, q, r);
  // fourValuePermutationIndex(r) == standard Lehmer rank of r.
  std::array<std::uint8_t, 4> ru{};
  for (int i = 0; i < 4; ++i) {
    ru[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(r[static_cast<std::size_t>(i)]);
  }
  return perm_rank_4(ru);
}

std::uint32_t upstream_middle_layer(const CubeState& state) {
  return upstream_comb495(state) * 24u + upstream_perm24(state);
}

void upstream_phase2_coords(const CubeState& state, Phase2Coords& out) noexcept {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::array<std::uint8_t, 8> perm{};
  for (int i = 0; i < 8; ++i) {
    perm[static_cast<std::size_t>(i)] =
        pc_corners[static_cast<std::size_t>(i)] >> 2;
  }
  out.corner_perm = perm_rank_8(perm);
  constexpr std::array<int, 8> ud_cubies = {0, 1, 2, 3, 8, 9, 10, 11};
  std::array<std::uint8_t, 8> seq{};
  for (int i = 0; i < 8; ++i) {
    const int pos =
        pc_edges[static_cast<std::size_t>(ud_cubies[static_cast<std::size_t>(i)])] >> 1;
    // Upstream returns -1 when a UD cubie sits in the slice (not in G1).
    // This loader returns 0 there (admissible-safe; phase-2 use is G1-only).
    if (pos >= 4 && pos <= 7) {
      out.ud_perm = 0;
      break;
    }
    seq[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(pos < 4 ? pos : pos - 4);
    if (i == 7) out.ud_perm = perm_rank_8(seq);
  }
  std::array<std::uint8_t, 4> seq4{};
  bool in_slice = true;
  for (int i = 0; i < 4; ++i) {
    const int pos = pc_edges[static_cast<std::size_t>(4 + i)] >> 1;
    if (pos < 4 || pos > 7) in_slice = false;
    seq4[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(pos - 4);
  }
  if (in_slice) {
    out.slice_perm = perm_rank_4(seq4);
  } else {
    std::array<int, 4> qq{};
    std::array<int, 4> rr{};
    slice_q_r(pc_edges, qq, rr);
    std::array<std::uint8_t, 4> ru{};
    for (int i = 0; i < 4; ++i) {
      ru[static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>(rr[static_cast<std::size_t>(i)]);
    }
    out.slice_perm = perm_rank_4(ru);
  }
}

std::uint32_t upstream_corner_perm(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::array<std::uint8_t, 8> perm{};
  for (int i = 0; i < 8; ++i) {
    perm[static_cast<std::size_t>(i)] =
        pc_corners[static_cast<std::size_t>(i)] >> 2;
  }
  return perm_rank_8(perm);
}

std::uint32_t upstream_ud_perm(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  // Cubies in order UB,UR,UF,UL,DB,DL,DF,DR (upstream ids 0..3,8..11).
  constexpr std::array<int, 8> ud_cubies = {0, 1, 2, 3, 8, 9, 10, 11};
  std::array<std::uint8_t, 8> seq{};
  for (int i = 0; i < 8; ++i) {
    const int pos =
        pc_edges[static_cast<std::size_t>(ud_cubies[static_cast<std::size_t>(i)])] >> 1;
    // Upstream returns -1 when a UD cubie sits in the slice (not in G1).
    // This loader returns 0 there (admissible-safe; phase-2 use is G1-only).
    if (pos >= 4 && pos <= 7) return 0;
    seq[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(pos < 4 ? pos : pos - 4);
  }
  return perm_rank_8(seq);
}

std::uint32_t upstream_slice_perm(const CubeState& state) {
  std::array<std::uint8_t, 8> pc_corners{};
  std::array<std::uint8_t, 12> pc_edges{};
  to_piece_cube(state, pc_corners, pc_edges);
  std::array<std::uint8_t, 4> seq{};
  bool in_slice = true;
  for (int i = 0; i < 4; ++i) {
    const int pos = pc_edges[static_cast<std::size_t>(4 + i)] >> 1;
    if (pos < 4 || pos > 7) in_slice = false;
    seq[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(pos - 4);
  }
  if (in_slice) return perm_rank_4(seq);
  // Outside G1 upstream (pos-4) is undefined; use relative-order rank
  // (identical to the in-slice value inside G1).
  return upstream_perm24(state);
}

std::uint64_t phase1_reduced_index(std::uint32_t class45,
                                   std::uint32_t flip_sym,
                                   std::uint32_t twist_sym) {
  return (static_cast<std::uint64_t>(class45) * 2048ull + flip_sym) * 2187ull +
         twist_sym;
}

std::uint64_t phase1_large_index(std::uint32_t class45, std::uint32_t flip_sym,
                                 std::uint32_t twist_sym, std::uint32_t mid_sym) {
  return phase1_reduced_index(class45, flip_sym, twist_sym) * 24ull +
         (mid_sym % 24u);
}

std::uint64_t phase2_index(std::uint32_t class2768, std::uint32_t ud_perm,
                           std::uint32_t slice_perm) {
  const std::uint64_t raw =
      (static_cast<std::uint64_t>(class2768) * 40320ull + ud_perm) * 24ull +
      slice_perm;
  return raw >> 1u;  // parity halving (corner^edge^slice even)
}

// -------------------------------------------------------------------------
// Directory resolution / fallback.
//
// Discovery order (documented in the header + CLI --help + README):
//   1. explicit --tables-dir / --data-dir / env (RUBIK_TABLES_DIR,
//      RUBIK_DATA_DIR) / set_tables_dir() override, if present;
//   2. ./tables/;  3. build/tables/;
//   4. <exe-dir>/tables/ + <exe-dir>/../tables/;
//   5. legacy ../tables, tables.
// Presence is tested by cornerHeuristicHTM existing inside the dir.
// -------------------------------------------------------------------------

bool dir_contains_tables(const std::filesystem::path& dir) {
  if (dir.empty()) return false;
  std::error_code ec;
  return std::filesystem::exists(dir / "cornerHeuristicHTM", ec);
}

std::filesystem::path executable_dir() {
#if defined(_WIN32)
  // GetModuleFileNameW via windows.h; 32768 covers long paths.
  std::vector<wchar_t> buf(32768);
  const DWORD n =
      ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
  if (n == 0 || n >= buf.size()) return {};
  std::filesystem::path p(std::wstring(buf.data(), n));
  if (p.has_parent_path()) return p.parent_path();
  return {};
#else
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return {};
  buf[n] = '\0';
  std::filesystem::path p(std::string(buf, static_cast<std::size_t>(n)));
  if (p.has_parent_path()) return p.parent_path();
  return {};
#endif
}

std::filesystem::path default_tables_dir() {
  // Discovery-aware default: prefer an actually-present tables dir so
  // callers that pin `default_tables_dir()` (e.g. the visualiser) still
  // find build/tables without a flag; otherwise the legacy fallback.
  std::error_code ec;
  std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (ec) cwd = std::filesystem::path(".");
  const std::filesystem::path found = first_tables_dir(
      tables_discovery_candidates(cwd, executable_dir()));
  if (!found.empty()) return found;
#if defined(_WIN32)
  return std::filesystem::path(".\\tables");
#else
  return std::filesystem::path("tables");
#endif
}

void set_tables_dir(const std::filesystem::path& dir) {
  std::lock_guard<std::mutex> lock(g_dir_mutex);
  if (dir.empty()) {
    g_override_dir.reset();
  } else {
    g_override_dir = dir;
  }
}

std::vector<std::filesystem::path> tables_discovery_candidates(
    const std::filesystem::path& cwd,
    const std::filesystem::path& exe_dir) {
  std::vector<std::filesystem::path> out;
  auto add = [&out](const std::filesystem::path& p) {
    if (p.empty()) return;
    for (const auto& q : out) {
      if (q == p) return;
    }
    out.push_back(p);
  };
  add(cwd / "tables");                    // (2) ./tables/
  add(cwd / "build" / "tables");          // (3) build/tables/
  if (!exe_dir.empty()) {                 // (4) exe-relative
    add(exe_dir / "tables");
    add(exe_dir / ".." / "tables");
  }
  add(cwd / ".." / "tables");             // (5) legacy ../tables
  add(cwd / "tables");                    // (5) legacy tables (deduped)
  // Legacy relative spellings kept working from a process cwd equal to
  // `cwd` (".\/tables", "../tables", "tables") via the cwd-joined paths.
  return out;
}

std::filesystem::path first_tables_dir(
    const std::vector<std::filesystem::path>& candidates) {
  for (const auto& c : candidates) {
    if (dir_contains_tables(c)) return c;
  }
  return {};
}

std::filesystem::path discover_tables_dir(
    const std::filesystem::path& explicit_dir,
    const std::filesystem::path& cwd,
    const std::filesystem::path& exe_dir) {
  if (!explicit_dir.empty()) return explicit_dir;
  if (const char* env = std::getenv("RUBIK_TABLES_DIR");
      env != nullptr && env[0] != '\0') {
    return std::filesystem::path(env);
  }
  if (const char* env = std::getenv("RUBIK_DATA_DIR");
      env != nullptr && env[0] != '\0') {
    return std::filesystem::path(env);
  }
  const std::filesystem::path found =
      first_tables_dir(tables_discovery_candidates(cwd, exe_dir));
  if (!found.empty()) return found;
  return {};  // caller falls back to default (h=0 Timeout)
}

std::filesystem::path resolve_tables_dir() {
  {
    std::lock_guard<std::mutex> lock(g_dir_mutex);
    if (g_override_dir.has_value() && !g_override_dir->empty()) {
      return *g_override_dir;
    }
  }
  if (const char* env = std::getenv("RUBIK_TABLES_DIR");
      env != nullptr && env[0] != '\0') {
    return std::filesystem::path(env);
  }
  if (const char* env = std::getenv("RUBIK_DATA_DIR");
      env != nullptr && env[0] != '\0') {
    return std::filesystem::path(env);
  }
  std::error_code ec;
  std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (ec) cwd = std::filesystem::path(".");
  const std::filesystem::path found = first_tables_dir(
      tables_discovery_candidates(cwd, executable_dir()));
  if (!found.empty()) return found;
  return default_tables_dir();
}

std::filesystem::path resolve_tables_dir(const std::filesystem::path& explicit_dir) {
  if (!explicit_dir.empty()) return explicit_dir;
  return resolve_tables_dir();
}

bool extract_tables_dir_flag(int argc, char* const argv[],
                             std::filesystem::path& out) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--tables-dir") {
      if (i + 1 < argc) {
        out = std::filesystem::path(argv[i + 1]);
        return true;
      }
      return false;  // missing value: caller treats as error
    }
  }
  return false;
}

LoadStatus describe_tables_dir(const std::filesystem::path& dir,
                               bool verify_hashes) {
  LoadStatus st;
  if (!is_little_endian()) {
    st.fell_back_to_data = true;
    st.message =
        "tables\\: host is not little-endian; falling back to data\\ DBs";
    return st;
  }
  static constexpr std::string_view kMinimal[] = {
      "cornerHeuristicHTM",         "edgePermutationHeuristicHTM",
      "kociembaPhase1HeuristicHTM", "kociembaPhase2HeuristicHTM",
      "kociembaPhase1Symmetry",     "kociembaPhase2Symmetry",
  };
  for (const std::string_view name : kMinimal) {
    const TableFileSpec& spec = find_spec(name);
    const std::filesystem::path path = dir / std::string(spec.filename);
    std::error_code ec;
    const std::uintmax_t fsize = std::filesystem::file_size(path, ec);
    if (ec) {
      st.fell_back_to_data = true;
      st.message = "tables\\: missing " + std::string(name) + " in " +
                   dir.string() +
                   "; falling back to data\\ DBs";
      return st;
    }
    if (fsize != spec.expected_bytes) {
      st.fell_back_to_data = true;
      st.message = "tables\\: size mismatch for " + std::string(name) + " (" +
                   std::to_string(fsize) + " != " +
                   std::to_string(spec.expected_bytes) +
                   "); falling back to data\\ DBs";
      return st;
    }
    if (verify_hashes && !sha256_file_matches(path, spec.sha256_hex)) {
      st.fell_back_to_data = true;
      st.message = "tables\\: SHA-256 mismatch for " + std::string(name) +
                   "; falling back to data\\ DBs";
      return st;
    }
  }
  st.ok = true;
  st.using_tables = true;
  st.fell_back_to_data = false;
  st.message = "tables\\: HTM set valid in " + dir.string();
  return st;
}

}  // namespace rubik::tables
