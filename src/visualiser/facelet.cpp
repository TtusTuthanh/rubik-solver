#include "visualiser/facelet.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "core/validation.hpp"

namespace rubik::visualiser {
namespace {

// Mirrors src/core/cube.cpp:12-72 (face order URFDLB, facelet index
// = face*9+pos, corner/edge facelet tables). Re-implemented here so
// this module can localize failures to offending sticker indices;
// to_facelets/parse_facelets remain the single source of truth for
// the actual conversion (see facelets_to_cube / cube_to_facelets).
constexpr char kFaceLetters[6] = {'U', 'R', 'F', 'D', 'L', 'B'};

int face_index(char c) {
  for (int i = 0; i < 6; ++i) {
    if (kFaceLetters[i] == c) return i;
  }
  return -1;
}

// Corner positions: facelets in order [primary (U/D), next, next].
constexpr int kCornerFacelets[8][3] = {
    {0 * 9 + 8, 1 * 9 + 0, 2 * 9 + 2},  // URF: U9, R1, F3
    {0 * 9 + 6, 2 * 9 + 0, 4 * 9 + 2},  // UFL: U7, F1, L3
    {0 * 9 + 0, 4 * 9 + 0, 5 * 9 + 2},  // ULB: U1, L1, B3
    {0 * 9 + 2, 5 * 9 + 0, 1 * 9 + 2},  // UBR: U3, B1, R3
    {3 * 9 + 2, 2 * 9 + 8, 1 * 9 + 6},  // DFR: D3, F9, R7
    {3 * 9 + 0, 4 * 9 + 8, 2 * 9 + 6},  // DLF: D1, L9, F7
    {3 * 9 + 6, 5 * 9 + 8, 4 * 9 + 6},  // DBL: D7, B9, L7
    {3 * 9 + 8, 1 * 9 + 8, 5 * 9 + 6},  // DRB: D9, R9, B7
};

// Edge positions: facelets in order [primary, other].
constexpr int kEdgeFacelets[12][2] = {
    {0 * 9 + 5, 1 * 9 + 1},  // UR: U6, R2
    {0 * 9 + 7, 2 * 9 + 1},  // UF: U8, F2
    {0 * 9 + 3, 4 * 9 + 1},  // UL: U4, L2
    {0 * 9 + 1, 5 * 9 + 1},  // UB: U2, B2
    {3 * 9 + 5, 1 * 9 + 7},  // DR: D6, R8
    {3 * 9 + 1, 2 * 9 + 7},  // DF: D2, F8
    {3 * 9 + 3, 4 * 9 + 7},  // DL: D4, L8
    {3 * 9 + 7, 5 * 9 + 7},  // DB: D8, B8
    {2 * 9 + 5, 1 * 9 + 3},  // FR: F6, R4
    {2 * 9 + 3, 4 * 9 + 5},  // FL: F4, L6
    {5 * 9 + 5, 4 * 9 + 3},  // BL: B6, L4
    {5 * 9 + 3, 1 * 9 + 5},  // BR: B4, R6
};

constexpr const char* kCornerNames[8] = {
    "URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB"};
constexpr const char* kEdgeNames[12] = {
    "UR", "UF", "UL", "UB", "DR", "DF", "DL", "DB", "FR", "FL", "BL", "BR"};

std::string solved_string() {
  return "UUUUUUUUU"
         "RRRRRRRRR"
         "FFFFFFFFF"
         "DDDDDDDDD"
         "LLLLLLLLL"
         "BBBBBBBBB";
}

// Colour set of each cubie id in home face order (primary first),
// derived from the solved facelets like corner_colours()/edge_colours()
// in src/core/cube.cpp:76-108.
const std::vector<std::vector<int>>& corner_colours() {
  static const std::vector<std::vector<int>> table = [] {
    const std::string solved = solved_string();
    std::vector<std::vector<int>> t;
    for (int p = 0; p < 8; ++p) {
      std::vector<int> cols;
      for (int k = 0; k < 3; ++k) {
        cols.push_back(face_index(solved[static_cast<std::size_t>(kCornerFacelets[p][k])]));
      }
      t.push_back(cols);
    }
    return t;
  }();
  return table;
}

const std::vector<std::vector<int>>& edge_colours() {
  static const std::vector<std::vector<int>> table = [] {
    const std::string solved = solved_string();
    std::vector<std::vector<int>> t;
    for (int p = 0; p < 12; ++p) {
      std::vector<int> cols;
      for (int k = 0; k < 2; ++k) {
        cols.push_back(face_index(solved[static_cast<std::size_t>(kEdgeFacelets[p][k])]));
      }
      t.push_back(cols);
    }
    return t;
  }();
  return table;
}

std::vector<int> sorted_colours(std::vector<int> v) {
  std::sort(v.begin(), v.end());
  return v;
}

int parity_of(const std::uint8_t* perm, int n) {
  int inversions = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (perm[i] > perm[j]) ++inversions;
    }
  }
  return inversions % 2;
}

} // namespace

const char* to_string(FaceletError error) noexcept {
  switch (error) {
    case FaceletError::Ok: return "Ok";
    case FaceletError::InvalidLength: return "InvalidLength";
    case FaceletError::InvalidCharacter: return "InvalidCharacter";
    case FaceletError::ColorCount: return "ColorCount";
    case FaceletError::DuplicateCenter: return "DuplicateCenter";
    case FaceletError::InvalidCubie: return "InvalidCubie";
    case FaceletError::DuplicatePiece: return "DuplicatePiece";
    case FaceletError::CornerTwist: return "CornerTwist";
    case FaceletError::EdgeFlip: return "EdgeFlip";
    case FaceletError::Parity: return "Parity";
  }
  return "Unknown";
}

FaceletValidation validate_facelets(std::string_view facelets) {
  auto fail = [](FaceletError error, std::string msg,
                 std::vector<int> off = {}) {
    return FaceletValidation{error, std::move(msg), std::move(off)};
  };

  if (facelets.size() != 54) {
    return fail(FaceletError::InvalidLength,
                std::format("facelet string length {} != 54", facelets.size()));
  }

  std::vector<int> bad_char;
  for (int i = 0; i < 54; ++i) {
    if (face_index(facelets[static_cast<std::size_t>(i)]) < 0) bad_char.push_back(i);
  }
  if (!bad_char.empty()) {
    const int i = bad_char.front();
    return fail(FaceletError::InvalidCharacter,
                std::format("invalid facelet character '{}' at index {}",
                            facelets[static_cast<std::size_t>(i)], i),
                bad_char);
  }

  std::array<int, 6> counts{};
  for (char c : facelets) counts[static_cast<std::size_t>(face_index(c))] += 1;
  for (int f = 0; f < 6; ++f) {
    if (counts[static_cast<std::size_t>(f)] != 9) {
      std::vector<int> off;
      for (int i = 0; i < 54; ++i) {
        if (face_index(facelets[static_cast<std::size_t>(i)]) == f) off.push_back(i);
      }
      return fail(FaceletError::ColorCount,
                  std::format("colour '{}' count {} != 9", kFaceLetters[f],
                              counts[static_cast<std::size_t>(f)]),
                  std::move(off));
    }
  }

  // Centres: 6 distinct centre colours.
  std::array<int, 6> centres{};
  for (int f = 0; f < 6; ++f) {
    centres[static_cast<std::size_t>(f)] =
        face_index(facelets[static_cast<std::size_t>(f * 9 + 4)]);
  }
  for (int f = 0; f < 6; ++f) {
    const int c = centres[static_cast<std::size_t>(f)];
    for (int g = 0; g < f; ++g) {
      if (centres[static_cast<std::size_t>(g)] == c) {
        std::vector<int> off;
        for (int h = 0; h < 6; ++h) {
          if (centres[static_cast<std::size_t>(h)] == c) off.push_back(h * 9 + 4);
        }
        return fail(FaceletError::DuplicateCenter,
                    std::format("duplicate centre colour '{}'", kFaceLetters[c]),
                    std::move(off));
      }
    }
  }

  // Corner cubies: validity, duplicates, orientation.
  const auto& ccols = corner_colours();
  std::array<std::uint8_t, 8> cp{};
  std::array<std::uint8_t, 8> co{};
  std::array<bool, 8> corner_seen{};
  for (int p = 0; p < 8; ++p) {
    std::vector<int> cols;
    std::vector<int> idx;
    for (int k = 0; k < 3; ++k) {
      idx.push_back(kCornerFacelets[p][k]);
      cols.push_back(face_index(facelets[static_cast<std::size_t>(kCornerFacelets[p][k])]));
    }
    const auto sorted = sorted_colours(cols);
    int cubie = -1;
    for (int c = 0; c < 8; ++c) {
      if (sorted_colours(ccols[static_cast<std::size_t>(c)]) == sorted) {
        cubie = c;
        break;
      }
    }
    if (cubie < 0) {
      return fail(FaceletError::InvalidCubie,
                  std::format("no corner cubie matches colours at position {} ({})",
                              p, kCornerNames[p]),
                  idx);
    }
    if (corner_seen[static_cast<std::size_t>(cubie)]) {
      return fail(FaceletError::DuplicatePiece,
                  std::format("duplicate corner cubie {} ('{}') at position {} ({})",
                              cubie, kCornerNames[cubie], p, kCornerNames[p]),
                  idx);
    }
    corner_seen[static_cast<std::size_t>(cubie)] = true;
    cp[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(cubie);
    const int primary = ccols[static_cast<std::size_t>(cubie)][0];
    int ori = 0;
    for (int k = 0; k < 3; ++k) {
      if (cols[static_cast<std::size_t>(k)] == primary) ori = k;
    }
    co[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(ori);
  }

  // Edge cubies: validity, duplicates, orientation.
  const auto& ecols = edge_colours();
  std::array<std::uint8_t, 12> ep{};
  std::array<std::uint8_t, 12> eo{};
  std::array<bool, 12> edge_seen{};
  for (int p = 0; p < 12; ++p) {
    std::vector<int> cols;
    std::vector<int> idx;
    for (int k = 0; k < 2; ++k) {
      idx.push_back(kEdgeFacelets[p][k]);
      cols.push_back(face_index(facelets[static_cast<std::size_t>(kEdgeFacelets[p][k])]));
    }
    const auto sorted = sorted_colours(cols);
    int cubie = -1;
    for (int c = 0; c < 12; ++c) {
      if (sorted_colours(ecols[static_cast<std::size_t>(c)]) == sorted) {
        cubie = c;
        break;
      }
    }
    if (cubie < 0) {
      return fail(FaceletError::InvalidCubie,
                  std::format("no edge cubie matches colours at position {} ({})",
                              p, kEdgeNames[p]),
                  idx);
    }
    if (edge_seen[static_cast<std::size_t>(cubie)]) {
      return fail(FaceletError::DuplicatePiece,
                  std::format("duplicate edge cubie {} ('{}') at position {} ({})",
                              cubie, kEdgeNames[cubie], p, kEdgeNames[p]),
                  idx);
    }
    edge_seen[static_cast<std::size_t>(cubie)] = true;
    ep[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(cubie);
    const int primary = ecols[static_cast<std::size_t>(cubie)][0];
    eo[static_cast<std::size_t>(p)] =
        static_cast<std::uint8_t>(cols[0] == primary ? 0 : 1);
  }

  int twist = 0;
  for (auto v : co) twist += v;
  if (twist % 3 != 0) {
    int p = 0;
    for (int i = 0; i < 8; ++i) {
      if (co[static_cast<std::size_t>(i)] != 0) {
        p = i;
        break;
      }
    }
    std::vector<int> off;
    for (int k = 0; k < 3; ++k) off.push_back(kCornerFacelets[p][k]);
    return fail(FaceletError::CornerTwist,
                std::format("corner twist sum {} not divisible by 3 "
                            "(twisted corner at position {} ({}))",
                            twist, p, kCornerNames[p]),
                std::move(off));
  }

  int flip = 0;
  for (auto v : eo) flip += v;
  if (flip % 2 != 0) {
    int p = 0;
    for (int i = 0; i < 12; ++i) {
      if (eo[static_cast<std::size_t>(i)] != 0) {
        p = i;
        break;
      }
    }
    std::vector<int> off;
    for (int k = 0; k < 2; ++k) off.push_back(kEdgeFacelets[p][k]);
    return fail(FaceletError::EdgeFlip,
                std::format("edge flip sum {} not divisible by 2 "
                            "(flipped edge at position {} ({}))",
                            flip, p, kEdgeNames[p]),
                std::move(off));
  }

  std::array<std::uint8_t, 6> centre_perm{};
  for (int f = 0; f < 6; ++f) {
    centre_perm[static_cast<std::size_t>(f)] = static_cast<std::uint8_t>(centres[static_cast<std::size_t>(f)]);
  }
  const int corner_parity = parity_of(cp.data(), 8);
  const int edge_parity = parity_of(ep.data(), 12);
  const int centre_parity = parity_of(centre_perm.data(), 6);
  if ((corner_parity ^ edge_parity ^ centre_parity) != 0) {
    return fail(FaceletError::Parity,
                "corner/edge/centre permutation parity is odd (parity swap)");
  }

  // Cross-check against the core validator so the two agree on every
  // input this module accepts (core returns a ValidationErrorKind,
  // which maps one-to-one onto the FaceletError kinds above).
  if (auto err = ::rubik::check_facelets(facelets)) {
    FaceletError mapped = FaceletError::InvalidCubie;
    switch (err->kind) {
      case ::rubik::ValidationErrorKind::InvalidLength: mapped = FaceletError::InvalidLength; break;
      case ::rubik::ValidationErrorKind::InvalidCharacter: mapped = FaceletError::InvalidCharacter; break;
      case ::rubik::ValidationErrorKind::ColourCount: mapped = FaceletError::ColorCount; break;
      case ::rubik::ValidationErrorKind::CentreError: mapped = FaceletError::DuplicateCenter; break;
      case ::rubik::ValidationErrorKind::DuplicateCubie: mapped = FaceletError::DuplicatePiece; break;
      case ::rubik::ValidationErrorKind::UnknownCubie: mapped = FaceletError::InvalidCubie; break;
      case ::rubik::ValidationErrorKind::TwistedCorner: mapped = FaceletError::CornerTwist; break;
      case ::rubik::ValidationErrorKind::FlippedEdge: mapped = FaceletError::EdgeFlip; break;
      case ::rubik::ValidationErrorKind::Parity: mapped = FaceletError::Parity; break;
    }
    return fail(mapped, err->message);
  }

  return fail(FaceletError::Ok, "ok");
}

FaceletValidation validate_facelets(const FaceletArray& facelets) {
  return validate_facelets(
      std::string_view(facelets.data(), facelets.size()));
}

std::expected<rubik::CubeState, FaceletValidation> facelets_to_cube(
    std::string_view facelets) {
  FaceletValidation validation = validate_facelets(facelets);
  if (validation.error != FaceletError::Ok) {
    return std::unexpected(validation);
  }
  auto parsed = ::rubik::parse_facelets(facelets);
  if (!parsed.has_value()) {
    // Should be unreachable given validate_facelets passed; classify
    // the core message defensively.
    const std::string msg = parsed.error();
    FaceletError error = FaceletError::InvalidCubie;
    if (msg.find("centre") != std::string::npos) {
      error = FaceletError::DuplicateCenter;
    } else if (msg.find("duplicate") != std::string::npos) {
      error = FaceletError::DuplicatePiece;
    }
    return std::unexpected(FaceletValidation{error, msg, {}});
  }
  return parsed.value();
}

std::expected<rubik::CubeState, FaceletValidation> facelets_to_cube(
    const FaceletArray& facelets) {
  return facelets_to_cube(
      std::string_view(facelets.data(), facelets.size()));
}

FaceletArray cube_to_facelets(const rubik::CubeState& state) {
  const std::string str = ::rubik::to_facelets(state);
  FaceletArray out{};
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = str[i];
  return out;
}

} // namespace rubik::visualiser
