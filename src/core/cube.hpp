#pragma once
// Cubie cube representation and 54-facelet conversion.
//
// Face order for facelets: U R F D L B (faces U,R,F,D,L,B in that order),
// each face 9 stickers row-major (1-9), as viewed looking at that face
// with the cube in solved orientation (U up, F front).
//
// Cubie indexing (Kociemba-style order):
//   Corners CP[0..7]:  URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB
//   Edges   EP[0..11]: UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR
//   CP[i] = cubie at corner position i, CO[i] in {0,1,2} = twist.
//   EP[i] = cubie at edge position i,   EO[i] in {0,1}   = flip.
// Uppercase names in the specification (CP/CO/EP/EO) are the lowercase
// members cp/co/ep/eo here; CP()/CO()/EP()/EO() accessors are provided.
#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace rubik {

// Face indices in URFDLB order.
inline constexpr int kFaceU = 0;
inline constexpr int kFaceR = 1;
inline constexpr int kFaceF = 2;
inline constexpr int kFaceD = 3;
inline constexpr int kFaceL = 4;
inline constexpr int kFaceB = 5;

struct CubeState {
  std::array<std::uint8_t, 8> cp{};  // corner permutation
  std::array<std::uint8_t, 8> co{};  // corner orientation (0..2)
  std::array<std::uint8_t, 12> ep{}; // edge permutation
  std::array<std::uint8_t, 12> eo{}; // edge orientation (0..1)
  // Centre cubie at each face position (URFDLB order); identity = 0..5.
  // Face moves keep centres fixed; slices/rotations move them, so this
  // is tracked to make whole-cube rotations representable in facelets.
  std::array<std::uint8_t, 6> centers{};

  bool operator==(const CubeState&) const = default;
  bool operator!=(const CubeState&) const = default;

  // Accessors using the specification's uppercase names.
  std::array<std::uint8_t, 8>& CP() { return cp; }
  const std::array<std::uint8_t, 8>& CP() const { return cp; }
  std::array<std::uint8_t, 8>& CO() { return co; }
  const std::array<std::uint8_t, 8>& CO() const { return co; }
  std::array<std::uint8_t, 12>& EP() { return ep; }
  const std::array<std::uint8_t, 12>& EP() const { return ep; }
  std::array<std::uint8_t, 12>& EO() { return eo; }
  const std::array<std::uint8_t, 12>& EO() const { return eo; }

  static CubeState solved();
  static CubeState identity() { return solved(); }
};

// Free-function helpers (aliases for discoverability).
CubeState identity();
bool is_solved(const CubeState& state);
bool solved(const CubeState& state); // alias of is_solved

// Facelet conversion. Error is a human-readable message; SolveError is
// defined in interface.hpp (which includes this header, so it cannot be
// used here without a circular include).
std::expected<CubeState, std::string> parse_facelets(std::string_view facelets);
std::string to_facelets(const CubeState& state);

// Compose two cubie states: apply `second`'s transformation after `first`.
// For move states this satisfies compose(apply(id,R), apply(id,R')) == id.
CubeState compose(const CubeState& first, const CubeState& second);

} // namespace rubik
