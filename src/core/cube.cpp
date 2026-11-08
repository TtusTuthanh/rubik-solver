#include "core/cube.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <vector>

namespace rubik {
namespace {

// Face order U R F D L B. Facelet index = face*9 + pos (row-major 0..8).
constexpr char kFaceLetters[6] = {'U', 'R', 'F', 'D', 'L', 'B'};

int face_index(char c) {
  for (int i = 0; i < 6; ++i) {
    if (kFaceLetters[i] == c) return i;
  }
  return -1;
}

// Corner positions: facelets in order [primary (U/D), next, next].
// The order follows clockwise around the corner viewed from outside:
// U corners follow R->F->L->B (clockwise from above),
// D corners follow F->R->B->L (clockwise from below).
constexpr int kCornerFacelets[8][3] = {
    // URF: U9, R1, F3
    {kFaceU * 9 + 8, kFaceR * 9 + 0, kFaceF * 9 + 2},
    // UFL: U7, F1, L3
    {kFaceU * 9 + 6, kFaceF * 9 + 0, kFaceL * 9 + 2},
    // ULB: U1, L1, B3
    {kFaceU * 9 + 0, kFaceL * 9 + 0, kFaceB * 9 + 2},
    // UBR: U3, B1, R3
    {kFaceU * 9 + 2, kFaceB * 9 + 0, kFaceR * 9 + 2},
    // DFR: D3, F9, R7
    {kFaceD * 9 + 2, kFaceF * 9 + 8, kFaceR * 9 + 6},
    // DLF: D1, L9, F7
    {kFaceD * 9 + 0, kFaceL * 9 + 8, kFaceF * 9 + 6},
    // DBL: D7, B9, L7
    {kFaceD * 9 + 6, kFaceB * 9 + 8, kFaceL * 9 + 6},
    // DRB: D9, R9, B7
    {kFaceD * 9 + 8, kFaceR * 9 + 8, kFaceB * 9 + 6},
};

// Edge positions: facelets in order [primary, other].
// Primary is U/D for U/D edges and F/B for middle-layer edges.
constexpr int kEdgeFacelets[12][2] = {
    // UR: U6, R2
    {kFaceU * 9 + 5, kFaceR * 9 + 1},
    // UF: U8, F2
    {kFaceU * 9 + 7, kFaceF * 9 + 1},
    // UL: U4, L2
    {kFaceU * 9 + 3, kFaceL * 9 + 1},
    // UB: U2, B2
    {kFaceU * 9 + 1, kFaceB * 9 + 1},
    // DR: D6, R8
    {kFaceD * 9 + 5, kFaceR * 9 + 7},
    // DF: D2, F8 (D face row 0 is the F/south side in our view)
    {kFaceD * 9 + 1, kFaceF * 9 + 7},
    // DL: D4, L8
    {kFaceD * 9 + 3, kFaceL * 9 + 7},
    // DB: D8, B8
    {kFaceD * 9 + 7, kFaceB * 9 + 7},
    // FR: F6, R4
    {kFaceF * 9 + 5, kFaceR * 9 + 3},
    // FL: F4, L6
    {kFaceF * 9 + 3, kFaceL * 9 + 5},
    // BL: B6, L4
    {kFaceB * 9 + 5, kFaceL * 9 + 3},
    // BR: B4, R6
    {kFaceB * 9 + 3, kFaceR * 9 + 5},
};

// Colour set of each cubie id, in its home face order (primary first).
// Derived from the facelet tables for the identity positions.
const std::vector<std::vector<int>>& corner_colours() {
  static const std::vector<std::vector<int>> table = [] {
    const std::string solved_str =
        "UUUUUUUUU" "RRRRRRRRR" "FFFFFFFFF" "DDDDDDDDD" "LLLLLLLLL" "BBBBBBBBB";
    std::vector<std::vector<int>> t;
    for (int p = 0; p < 8; ++p) {
      std::vector<int> cols;
      for (int k = 0; k < 3; ++k) {
        cols.push_back(face_index(solved_str[static_cast<std::size_t>(kCornerFacelets[p][k])]));
      }
      t.push_back(cols);
    }
    return t;
  }();
  return table;
}

const std::vector<std::vector<int>>& edge_colours() {
  static const std::vector<std::vector<int>> table = [] {
    const std::string solved_str =
        "UUUUUUUUU" "RRRRRRRRR" "FFFFFFFFF" "DDDDDDDDD" "LLLLLLLLL" "BBBBBBBBB";
    std::vector<std::vector<int>> t;
    for (int p = 0; p < 12; ++p) {
      std::vector<int> cols;
      for (int k = 0; k < 2; ++k) {
        cols.push_back(face_index(solved_str[static_cast<std::size_t>(kEdgeFacelets[p][k])]));
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

} // namespace

CubeState CubeState::solved() {
  CubeState s;
  for (std::uint8_t i = 0; i < 8; ++i) {
    s.cp[i] = i;
    s.co[i] = 0;
  }
  for (std::uint8_t i = 0; i < 12; ++i) {
    s.ep[i] = i;
    s.eo[i] = 0;
  }
  for (std::uint8_t i = 0; i < 6; ++i) s.centers[i] = i;
  return s;
}

CubeState identity() { return CubeState::solved(); }

bool is_solved(const CubeState& state) { return state == CubeState::solved(); }
bool solved(const CubeState& state) { return is_solved(state); }

std::expected<CubeState, std::string> parse_facelets(std::string_view facelets) {
  if (facelets.size() != 54) {
    return std::unexpected(
        std::format("facelet string length {} != 54", facelets.size()));
  }
  for (char c : facelets) {
    if (face_index(c) < 0) {
      return std::unexpected(
          std::format("invalid facelet character '{}'", c));
    }
  }

  CubeState s;
  // Centres.
  std::set<int> centre_seen;
  for (int f = 0; f < 6; ++f) {
    const int idx = face_index(facelets[static_cast<std::size_t>(f * 9 + 4)]);
    if (centre_seen.count(idx) != 0) {
      return std::unexpected(std::string("duplicate centre colour"));
    }
    centre_seen.insert(idx);
    s.centers[static_cast<std::size_t>(f)] = static_cast<std::uint8_t>(idx);
  }

  // Corners.
  const auto& ccols = corner_colours();
  std::set<int> corner_seen;
  for (int p = 0; p < 8; ++p) {
    std::vector<int> cols;
    for (int k = 0; k < 3; ++k) {
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
      return std::unexpected(
          std::format("no corner cubie matches colours at position {}", p));
    }
    if (corner_seen.count(cubie) != 0) {
      return std::unexpected(
          std::format("duplicate corner cubie {}", cubie));
    }
    corner_seen.insert(cubie);
    s.cp[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(cubie);
    const int primary = ccols[static_cast<std::size_t>(cubie)][0];
    int ori = -1;
    for (int k = 0; k < 3; ++k) {
      if (cols[static_cast<std::size_t>(k)] == primary) ori = k;
    }
    s.co[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(ori);
  }

  // Edges.
  const auto& ecols = edge_colours();
  std::set<int> edge_seen;
  for (int p = 0; p < 12; ++p) {
    std::vector<int> cols;
    for (int k = 0; k < 2; ++k) {
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
      return std::unexpected(
          std::format("no edge cubie matches colours at position {}", p));
    }
    if (edge_seen.count(cubie) != 0) {
      return std::unexpected(std::format("duplicate edge cubie {}", cubie));
    }
    edge_seen.insert(cubie);
    s.ep[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(cubie);
    const int primary = ecols[static_cast<std::size_t>(cubie)][0];
    const int ori = (cols[0] == primary) ? 0 : 1;
    s.eo[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(ori);
  }

  return s;
}

std::string to_facelets(const CubeState& state) {
  std::string out(54, '?');
  // Centres.
  for (int f = 0; f < 6; ++f) {
    out[static_cast<std::size_t>(f * 9 + 4)] =
        kFaceLetters[state.centers[static_cast<std::size_t>(f)]];
  }
  // Corners: cubie colour at home-order index k goes to position face
  // index (co + k) % 3 in the position's face order.
  const auto& ccols = corner_colours();
  for (int p = 0; p < 8; ++p) {
    const int cubie = state.cp[static_cast<std::size_t>(p)];
    const int co = state.co[static_cast<std::size_t>(p)];
    for (int k = 0; k < 3; ++k) {
      const int colour_face = ccols[static_cast<std::size_t>(cubie)][static_cast<std::size_t>(k)];
      const int dest = (co + k) % 3;
      out[static_cast<std::size_t>(kCornerFacelets[p][dest])] =
          kFaceLetters[colour_face];
    }
  }
  // Edges: colour at home-order index k goes to position face (eo+k)%2.
  const auto& ecols = edge_colours();
  for (int p = 0; p < 12; ++p) {
    const int cubie = state.ep[static_cast<std::size_t>(p)];
    const int eo = state.eo[static_cast<std::size_t>(p)];
    for (int k = 0; k < 2; ++k) {
      const int colour_face = ecols[static_cast<std::size_t>(cubie)][static_cast<std::size_t>(k)];
      const int dest = (eo + k) % 2;
      out[static_cast<std::size_t>(kEdgeFacelets[p][dest])] =
          kFaceLetters[colour_face];
    }
  }
  return out;
}

CubeState compose(const CubeState& first, const CubeState& second) {
  // Apply `second`'s sticker permutation to `first`'s facelets.
  // The sticker permutation of a cubie state is obtained from cubie
  // identities: a sticker of colour X on cubie c originated at the
  // solved facelet for (c, X). This is equivalent to the standard
  // cubie multiplication but is computed via facelets so it stays
  // consistent with parse_facelets/to_facelets by construction.
  const std::string first_f = to_facelets(first);
  const std::string second_f = to_facelets(second);
  const std::string solved_f = to_facelets(CubeState::solved());

  // origin_of[facelet in second] = index in solved facelets, keyed by
  // (cubie, colour) identity. Instead build: for each position in
  // second, the facelet there came from solved position = home of cubie.
  // We recover that home by scanning solved facelets for the cubie:
  // simpler: build permutation perm where result[dest] = first[src],
  // src = solved index of the cubie sticker now at dest in `second`.
  // A cubie sticker is identified by (cubie id, colour). For corners
  // and edges we know cubie + colours directly; for centres likewise.
  // Map (cubie kind, cubie id, colour) -> solved facelet index.
  // Home facelet tables are the same as position tables for identity.
  std::string result(54, '?');
  // Centres of result.
  for (int f = 0; f < 6; ++f) {
    const int cubie = second.centers[static_cast<std::size_t>(f)];
    // Centre sticker of cubie `cubie` in solved cube is at face cubie.
    const int src = cubie * 9 + 4;
    result[static_cast<std::size_t>(f * 9 + 4)] =
        first_f[static_cast<std::size_t>(src)];
  }
  // Corners.
  for (int p = 0; p < 8; ++p) {
    const int cubie = second.cp[static_cast<std::size_t>(p)];
    const int co = second.co[static_cast<std::size_t>(p)];
    for (int k = 0; k < 3; ++k) {
      const int dest_face_idx = (co + k) % 3;
      const int dest = kCornerFacelets[p][dest_face_idx];
      // Solved index of this cubie's sticker with this colour: home
      // position is cubie id, face order index k.
      const int src = kCornerFacelets[cubie][k];
      result[static_cast<std::size_t>(dest)] = first_f[static_cast<std::size_t>(src)];
    }
  }
  // Edges.
  for (int p = 0; p < 12; ++p) {
    const int cubie = second.ep[static_cast<std::size_t>(p)];
    const int eo = second.eo[static_cast<std::size_t>(p)];
    for (int k = 0; k < 2; ++k) {
      const int dest_face_idx = (eo + k) % 2;
      const int dest = kEdgeFacelets[p][dest_face_idx];
      const int src = kEdgeFacelets[cubie][k];
      result[static_cast<std::size_t>(dest)] = first_f[static_cast<std::size_t>(src)];
    }
  }
  (void)solved_f;
  (void)second_f;
  auto parsed = parse_facelets(result);
  if (parsed.has_value()) return parsed.value();
  // Unreachable for valid cubie inputs; fall back to first.
  return first;
}

} // namespace rubik
