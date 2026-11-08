#include "core/moves.hpp"

#include <array>
#include <cctype>
#include <format>
#include <map>
#include <sstream>
#include <string>
#include <tuple>

#include "core/cube.hpp"

namespace rubik {
namespace {

struct Vec3 {
  int x = 0, y = 0, z = 0;
  bool operator==(const Vec3&) const = default;
};

struct Sticker {
  Vec3 pos;    // cubie position
  Vec3 normal; // face normal
};

// Facelet index -> sticker coordinate, using the same face views as
// cube.cpp: each face viewed from outside, row-major.
// Axes: x -1=L +1=R, y -1=D +1=U, z -1=B +1=F.
const std::array<Sticker, 54>& sticker_table() {
  static const std::array<Sticker, 54> table = [] {
    std::array<Sticker, 54> t{};
    auto set = [&](int face, int r, int c, Vec3 pos, Vec3 normal) {
      t[static_cast<std::size_t>(face * 9 + r * 3 + c)] = Sticker{pos, normal};
    };
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        // U: y=+1, x=c-1, z=r-1
        set(kFaceU, r, c, Vec3{c - 1, 1, r - 1}, Vec3{0, 1, 0});
        // R: x=+1, y=1-r, z=1-c
        set(kFaceR, r, c, Vec3{1, 1 - r, 1 - c}, Vec3{1, 0, 0});
        // F: z=+1, y=1-r, x=c-1
        set(kFaceF, r, c, Vec3{c - 1, 1 - r, 1}, Vec3{0, 0, 1});
        // D: y=-1, z=1-r, x=c-1
        set(kFaceD, r, c, Vec3{c - 1, -1, 1 - r}, Vec3{0, -1, 0});
        // L: x=-1, y=1-r, z=c-1
        set(kFaceL, r, c, Vec3{-1, 1 - r, c - 1}, Vec3{-1, 0, 0});
        // B: z=-1, y=1-r, x=1-c
        set(kFaceB, r, c, Vec3{1 - c, 1 - r, -1}, Vec3{0, 0, -1});
      }
    }
    return t;
  }();
  return table;
}

std::map<std::tuple<int, int, int, int, int, int>, int> sticker_index() {
  std::map<std::tuple<int, int, int, int, int, int>, int> m;
  const auto& t = sticker_table();
  for (int i = 0; i < 54; ++i) {
    const auto& s = t[static_cast<std::size_t>(i)];
    m[std::make_tuple(s.pos.x, s.pos.y, s.pos.z, s.normal.x, s.normal.y, s.normal.z)] = i;
  }
  return m;
}

Vec3 rot_x(Vec3 v, int quarter_turns) {
  // +90 around +x: (y,z)->(-z,y)
  for (int i = 0; i < quarter_turns; ++i) v = Vec3{v.x, -v.z, v.y};
  return v;
}
Vec3 rot_y(Vec3 v, int quarter_turns) {
  // +90 around +y: (x,z)->(z,-x)
  for (int i = 0; i < quarter_turns; ++i) v = Vec3{v.z, v.y, -v.x};
  return v;
}
Vec3 rot_z(Vec3 v, int quarter_turns) {
  // +90 around +z: (x,y)->(-y,x)
  for (int i = 0; i < quarter_turns; ++i) v = Vec3{-v.y, v.x, v.z};
  return v;
}

enum class Axis { X, Y, Z };

Vec3 rotate(Vec3 v, Axis axis, int quarter_turns) {
  if (axis == Axis::X) return rot_x(v, quarter_turns);
  if (axis == Axis::Y) return rot_y(v, quarter_turns);
  return rot_z(v, quarter_turns);
}

// Permutation for one clockwise quarter turn of a base move.
// Returns perm[new_index] = old_index.
std::array<int, 54> base_permutation(char base) {
  const auto& t = sticker_table();
  const auto idx = sticker_index();
  std::array<int, 54> perm{};
  for (int i = 0; i < 54; ++i) perm[static_cast<std::size_t>(i)] = i;

  auto apply_layer = [&](auto&& pred, Axis axis, int turns) {
    for (int i = 0; i < 54; ++i) {
      const auto& s = t[static_cast<std::size_t>(i)];
      if (!pred(s.pos)) continue;
      const Vec3 np = rotate(s.pos, axis, turns);
      const Vec3 nn = rotate(s.normal, axis, turns);
      const auto it = idx.find(std::make_tuple(np.x, np.y, np.z, nn.x, nn.y, nn.z));
      if (it != idx.end()) {
        perm[static_cast<std::size_t>(it->second)] = i;
      }
    }
  };

  switch (base) {
    case 'R': apply_layer([](Vec3 p) { return p.x == 1; }, Axis::X, 3); break; // CW from east = -90
    case 'L': apply_layer([](Vec3 p) { return p.x == -1; }, Axis::X, 1); break;
    case 'U': apply_layer([](Vec3 p) { return p.y == 1; }, Axis::Y, 3); break;
    case 'D': apply_layer([](Vec3 p) { return p.y == -1; }, Axis::Y, 1); break;
    case 'F': apply_layer([](Vec3 p) { return p.z == 1; }, Axis::Z, 3); break;
    case 'B': apply_layer([](Vec3 p) { return p.z == -1; }, Axis::Z, 1); break;
    case 'M': apply_layer([](Vec3 p) { return p.x == 0; }, Axis::X, 1); break; // like L
    case 'E': apply_layer([](Vec3 p) { return p.y == 0; }, Axis::Y, 1); break; // like D
    case 'S': apply_layer([](Vec3 p) { return p.z == 0; }, Axis::Z, 3); break; // like F
    case 'x': apply_layer([](Vec3) { return true; }, Axis::X, 3); break;      // like R
    case 'y': apply_layer([](Vec3) { return true; }, Axis::Y, 3); break;      // like U
    case 'z': apply_layer([](Vec3) { return true; }, Axis::Z, 3); break;      // like F
    default: break;
  }
  return perm;
}

std::array<int, 54> permutation_for_token(const std::string& token) {
  const char base = token.empty() ? '\0' : token[0];
  const std::string mod = token.size() > 1 ? std::string(1, token[1]) : "";
  const auto base_perm = base_permutation(base);
  std::array<int, 54> result{};
  for (int i = 0; i < 54; ++i) result[static_cast<std::size_t>(i)] = i;
  int times = 1;
  if (mod == "'") times = 3;
  if (mod == "2") times = 2;
  for (int t = 0; t < times; ++t) {
    std::array<int, 54> next{};
    for (int i = 0; i < 54; ++i) {
      next[static_cast<std::size_t>(i)] = result[static_cast<std::size_t>(base_perm[static_cast<std::size_t>(i)])];
    }
    result = next;
  }
  return result;
}

bool is_base(char c) {
  return c == 'U' || c == 'R' || c == 'F' || c == 'D' || c == 'L' || c == 'B' ||
         c == 'M' || c == 'E' || c == 'S' || c == 'x' || c == 'y' || c == 'z';
}

} // namespace

bool is_valid_move_token(std::string_view token) {
  if (token.empty() || token.size() > 2) return false;
  if (!is_base(token[0])) return false;
  if (token.size() == 2) return token[1] == '\'' || token[1] == '2';
  return true;
}

std::expected<std::vector<Move>, std::string> parse_moves(std::string_view text) {
  std::vector<Move> out;
  std::istringstream iss{std::string(text)};
  std::string tok;
  while (iss >> tok) {
    if (!is_valid_move_token(tok)) {
      return std::unexpected(std::format("invalid move token '{}'", tok));
    }
    out.push_back(Move{tok});
  }
  return out;
}

std::string format_moves(const std::vector<Move>& moves) {
  std::string out;
  for (std::size_t i = 0; i < moves.size(); ++i) {
    if (i != 0) out += ' ';
    out += moves[i].token;
  }
  return out;
}

std::vector<Move> all_face_moves() {
  std::vector<Move> out;
  for (char f : {'U', 'R', 'F', 'D', 'L', 'B'}) {
    out.push_back(Move{std::string(1, f)});
    out.push_back(Move{std::string(1, f) + "'"});
    out.push_back(Move{std::string(1, f) + "2"});
  }
  return out;
}

Move inverse_move(const Move& move) {
  if (move.token.size() == 2 && move.token[1] == '\'') {
    return Move{std::string(1, move.token[0])};
  }
  if (move.token.size() == 2 && move.token[1] == '2') {
    return move; // X2 is its own inverse
  }
  return Move{move.token + "'"};
}

std::vector<Move> inverse_moves(const std::vector<Move>& moves) {
  std::vector<Move> out;
  out.reserve(moves.size());
  for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
    out.push_back(inverse_move(*it));
  }
  return out;
}

CubeState apply(const CubeState& state, const Move& move) {
  if (!is_valid_move_token(move.token)) return state;
  const auto perm = permutation_for_token(move.token);
  const std::string before = to_facelets(state);
  std::string after(54, '?');
  for (int i = 0; i < 54; ++i) {
    after[static_cast<std::size_t>(i)] =
        before[static_cast<std::size_t>(perm[static_cast<std::size_t>(i)])];
  }
  auto parsed = parse_facelets(after);
  if (parsed.has_value()) return parsed.value();
  return state; // unreachable for valid states/moves
}

CubeState apply(const CubeState& state, const std::vector<Move>& moves) {
  CubeState cur = state;
  for (const auto& m : moves) cur = apply(cur, m);
  return cur;
}

} // namespace rubik
