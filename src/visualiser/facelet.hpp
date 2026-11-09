#pragma once

// Pure C++ facelet module: validation and conversion between
// 54-facelet strings and rubik::CubeState. No GUI.
//
// Discovered convention (verified against project code, not assumed):
//   Face order URFDLB (kFaceU=0, kFaceR=1, kFaceF=2, kFaceD=3,
//   kFaceL=4, kFaceB=5); facelet index = face*9 + row*3 + col with
//   row-major pos 0..8 per face; sticker chars 'U','R','F','D','L','B';
//   centres at face*9+4.
//   Corners CP[0..7]: URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB
//   Edges   EP[0..11]: UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR
//   (Kociemba-style cubie order as proven by the project sources below;
//   not assumed from external knowledge.)
// Sources: src/core/cube.hpp:4-14 (face/cubic order), src/core/cube.hpp:31-39
//   (CubeState layout cp[8], co[8], ep[12], eo[12], centers[6]),
//   src/core/cube.cpp:12 (facelet index), src/core/cube.cpp:26-72
//   (corner/edge facelet tables), src/core/cube.cpp:136 (parse_facelets),
//   src/core/cube.cpp:227 (to_facelets), src/core/validation.cpp:24
//   (check_facelets).
//
// Validation logic lives in this module (thin wrappers mirroring
// rubik::check_facelets in src/core/validation.cpp) so that failures
// map to distinct FaceletError codes with offending sticker indices,
// instead of collapsing into a single message. The check order matches
// the core validator: length, characters, colour counts, centres,
// corner cubies (valid/duplicates), edge cubies (valid/duplicates),
// corner twist sum %3, edge flip sum %2, permutation parity. The core
// validator (rubik::check_facelets / validate_facelets_detailed) is
// additionally used as a final cross-check once this module accepts
// the input, so the two never drift. Note the core validator's parity
// rule is total parity (corner ^ edge ^ centre == 0, see
// validation.cpp:73-82); with fixed centres this is exactly
// corner parity == edge parity, and it also admits physical
// slice/rotation states.

#include <array>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "core/cube.hpp"

namespace rubik::visualiser {

// Facelet error kinds. Twist, flip and parity failures are distinct
// values so callers can highlight them differently.
enum class FaceletError {
  Ok,
  InvalidLength,
  InvalidCharacter,
  ColorCount,
  DuplicateCenter,
  InvalidCubie,
  DuplicatePiece,
  CornerTwist,
  EdgeFlip,
  Parity,
};

// Human-readable name for a FaceletError (for UI highlights / logs).
const char* to_string(FaceletError error) noexcept;

// Detailed validation result. `offending` holds sticker indices
// (0..53) worth highlighting: for twist the 3 stickers of the first
// twisted corner, for flip the 2 stickers of the first flipped edge,
// for cubie errors the stickers of the offending position, for
// duplicate centres the involved centre indices, for colour counts
// the indices holding the offending colour. It is empty when the
// failure is not localizable (length, parity).
struct FaceletValidation {
  FaceletError error;
  std::string message;
  std::vector<int> offending;
};

// sticker values: 'U','R','F','D','L','B'
using FaceletArray = std::array<char, 54>;

// Validate 54 facelets (fixed-size input: length is 54 by
// construction, so InvalidLength is unreachable here; use the
// std::string_view overload to exercise it).
FaceletValidation validate_facelets(const FaceletArray& facelets);

// Validate a facelet string. Fails with InvalidLength when the
// length is not exactly 54.
FaceletValidation validate_facelets(std::string_view facelets);

// Convert facelets to a cube state. Returns the detailed validation
// failure as the error when the input is invalid.
std::expected<rubik::CubeState, FaceletValidation> facelets_to_cube(
    const FaceletArray& facelets);

// Convert a facelet string to a cube state (length-checked).
std::expected<rubik::CubeState, FaceletValidation> facelets_to_cube(
    std::string_view facelets);

// Convert a cube state to its 54 facelets.
FaceletArray cube_to_facelets(const rubik::CubeState& state);

} // namespace rubik::visualiser
