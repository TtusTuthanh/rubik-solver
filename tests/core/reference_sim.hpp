#pragma once
// Independent facelet-permutation reference simulator (tests only).
// This file deliberately does NOT include or share code with
// src/core/moves.cpp / cube.cpp: permutations are hand-written facelet
// cycles over the URFDLB facelet layout, whereas the core derives its
// permutations from 3D sticker coordinates. The two implementations
// cross-check each other in the tests.
//
// Facelet layout: faces U R F D L B, each 9 stickers row-major.
// Index = face*9 + pos, faces in that order, pos (r,c) -> r*3+c.
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace refsim {

inline int idx(int face, int pos) { return face * 9 + pos; }
inline constexpr int U = 0, R = 1, F = 2, D = 3, L = 4, B = 5;

inline std::string solved_facelets() {
  return "UUUUUUUUU" "RRRRRRRRR" "FFFFFFFFF" "DDDDDDDDD" "LLLLLLLLL" "BBBBBBBBB";
}

// Apply one 4-cycle of facelet indices: new[c1]=old[c0], new[c2]=old[c1],
// new[c3]=old[c2], new[c0]=old[c3] (stickers move c0->c1->c2->c3->c0).
inline void cycle4(std::string& s, std::array<int, 4> c) {
  const char v0 = s[static_cast<std::size_t>(c[0])];
  const char v1 = s[static_cast<std::size_t>(c[1])];
  const char v2 = s[static_cast<std::size_t>(c[2])];
  const char v3 = s[static_cast<std::size_t>(c[3])];
  s[static_cast<std::size_t>(c[1])] = v0;
  s[static_cast<std::size_t>(c[2])] = v1;
  s[static_cast<std::size_t>(c[3])] = v2;
  s[static_cast<std::size_t>(c[0])] = v3;
}

inline void rotate_face_cw(std::string& s, int face) {
  // Sticker at (r,c) moves to (c, 2-r).
  std::array<char, 9> old{};
  for (int i = 0; i < 9; ++i) old[static_cast<std::size_t>(i)] = s[static_cast<std::size_t>(idx(face, i))];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      s[static_cast<std::size_t>(idx(face, c * 3 + (2 - r)))] =
          old[static_cast<std::size_t>(r * 3 + c)];
    }
  }
}

inline std::string apply_base(std::string s, char base) {
  // Columns/rows/cycles below were traced cubie-by-cubie for each face.
  if (base == 'U') {
    rotate_face_cw(s, U);
    // (B0,R0,F0,L0), (B1,R1,F1,L1), (B2,R2,F2,L2)
    for (int k = 0; k < 3; ++k) {
      cycle4(s, {idx(B, k), idx(R, k), idx(F, k), idx(L, k)});
    }
  } else if (base == 'D') {
    rotate_face_cw(s, D);
    // (F6,R6,B6,L6), (F7,R7,B7,L7), (F8,R8,B8,L8)
    for (int k = 0; k < 3; ++k) {
      cycle4(s, {idx(F, 6 + k), idx(R, 6 + k), idx(B, 6 + k), idx(L, 6 + k)});
    }
  } else if (base == 'R') {
    rotate_face_cw(s, R);
    cycle4(s, {idx(U, 2), idx(B, 6), idx(D, 2), idx(F, 2)});
    cycle4(s, {idx(U, 5), idx(B, 3), idx(D, 5), idx(F, 5)});
    cycle4(s, {idx(U, 8), idx(B, 0), idx(D, 8), idx(F, 8)});
  } else if (base == 'L') {
    rotate_face_cw(s, L);
    cycle4(s, {idx(U, 0), idx(F, 0), idx(D, 0), idx(B, 8)});
    cycle4(s, {idx(U, 3), idx(F, 3), idx(D, 3), idx(B, 5)});
    cycle4(s, {idx(U, 6), idx(F, 6), idx(D, 6), idx(B, 2)});
  } else if (base == 'F') {
    rotate_face_cw(s, F);
    cycle4(s, {idx(U, 6), idx(R, 0), idx(D, 2), idx(L, 8)});
    cycle4(s, {idx(U, 7), idx(R, 3), idx(D, 1), idx(L, 5)});
    cycle4(s, {idx(U, 8), idx(R, 6), idx(D, 0), idx(L, 2)});
  } else if (base == 'B') {
    rotate_face_cw(s, B);
    cycle4(s, {idx(U, 0), idx(L, 6), idx(D, 8), idx(R, 2)});
    cycle4(s, {idx(U, 1), idx(L, 3), idx(D, 7), idx(R, 5)});
    cycle4(s, {idx(U, 2), idx(L, 0), idx(D, 6), idx(R, 8)});
  } else if (base == 'M') {
    cycle4(s, {idx(U, 1), idx(F, 1), idx(D, 1), idx(B, 7)});
    cycle4(s, {idx(U, 4), idx(F, 4), idx(D, 4), idx(B, 4)});
    cycle4(s, {idx(U, 7), idx(F, 7), idx(D, 7), idx(B, 1)});
  } else if (base == 'E') {
    cycle4(s, {idx(F, 3), idx(R, 3), idx(B, 3), idx(L, 3)});
    cycle4(s, {idx(F, 4), idx(R, 4), idx(B, 4), idx(L, 4)});
    cycle4(s, {idx(F, 5), idx(R, 5), idx(B, 5), idx(L, 5)});
  } else if (base == 'S') {
    cycle4(s, {idx(U, 3), idx(R, 1), idx(D, 5), idx(L, 7)});
    cycle4(s, {idx(U, 4), idx(R, 4), idx(D, 4), idx(L, 4)});
    cycle4(s, {idx(U, 5), idx(R, 7), idx(D, 3), idx(L, 1)});
  }
  return s;
}

inline std::string apply_token(std::string s, std::string_view token) {
  if (token.empty()) return s;
  const char base = token[0];
  // Rotations as face+slice combinations (independently written here).
  if (base == 'x') {
    // x = R M' L' (times = 1, 3, 3 for ', 2 for 2)
    const int times = token.size() > 1 && token[1] == '\'' ? 3 : (token.size() > 1 && token[1] == '2' ? 2 : 1);
    for (int i = 0; i < times; ++i) {
      s = apply_base(s, 'R');
      s = apply_base(s, 'M'); s = apply_base(s, 'M'); s = apply_base(s, 'M'); // M'
      s = apply_base(s, 'L'); s = apply_base(s, 'L'); s = apply_base(s, 'L'); // L'
    }
    return s;
  }
  if (base == 'y') {
    const int times = token.size() > 1 && token[1] == '\'' ? 3 : (token.size() > 1 && token[1] == '2' ? 2 : 1);
    for (int i = 0; i < times; ++i) {
      s = apply_base(s, 'U');
      s = apply_base(s, 'E'); s = apply_base(s, 'E'); s = apply_base(s, 'E'); // E'
      s = apply_base(s, 'D'); s = apply_base(s, 'D'); s = apply_base(s, 'D'); // D'
    }
    return s;
  }
  if (base == 'z') {
    const int times = token.size() > 1 && token[1] == '\'' ? 3 : (token.size() > 1 && token[1] == '2' ? 2 : 1);
    for (int i = 0; i < times; ++i) {
      s = apply_base(s, 'F');
      s = apply_base(s, 'S'); // S (like F)
      s = apply_base(s, 'B'); s = apply_base(s, 'B'); s = apply_base(s, 'B'); // B'
    }
    return s;
  }
  const int times = token.size() > 1 && token[1] == '\'' ? 3 : (token.size() > 1 && token[1] == '2' ? 2 : 1);
  for (int i = 0; i < times; ++i) s = apply_base(s, base);
  return s;
}

inline std::string apply_sequence(std::string s, const std::vector<std::string>& tokens) {
  for (const auto& t : tokens) s = apply_token(s, t);
  return s;
}

inline std::string inverse_token(std::string_view token) {
  if (token.size() > 1 && token[1] == '\'') return std::string(1, token[0]);
  if (token.size() > 1 && token[1] == '2') return std::string(token);
  return std::string(token) + "'";
}

inline std::vector<std::string> inverse_sequence(const std::vector<std::string>& tokens) {
  std::vector<std::string> out;
  for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) out.push_back(inverse_token(*it));
  return out;
}

} // namespace refsim
