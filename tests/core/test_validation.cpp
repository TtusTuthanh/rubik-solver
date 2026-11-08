#include <gtest/gtest.h>

#include <string>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "core/validation.hpp"

using namespace rubik;

namespace {

std::string facelets_of(const CubeState& s) { return to_facelets(s); }

std::string scrambled(const std::string& scramble) {
  auto moves = parse_moves(scramble);
  if (!moves.has_value()) return {};
  return facelets_of(apply(CubeState::solved(), moves.value()));
}

} // namespace

TEST(ValidationTest, SolvedAndScrambledAreValid) {
  EXPECT_TRUE(validate_facelets(facelets_of(CubeState::solved())).has_value());
  EXPECT_TRUE(validate_facelets(scrambled("R U R' U'")).has_value());
  EXPECT_TRUE(validate_facelets(scrambled("F2 D' L B2 M E S")).has_value());
  EXPECT_TRUE(validate_facelets_detailed(facelets_of(CubeState::solved())).has_value());
  EXPECT_FALSE(check_facelets(facelets_of(CubeState::solved())).has_value());
}

TEST(ValidationTest, RejectsBadLengthAndCharacter) {
  auto bad_len = validate_facelets_detailed("UUU");
  ASSERT_FALSE(bad_len.has_value());
  EXPECT_EQ(bad_len.error().kind, ValidationErrorKind::InvalidLength);
  EXPECT_EQ(validate_facelets("UUU").error(), SolveError::InvalidInput);

  std::string bad_char(54, 'U');
  bad_char[0] = 'X';
  // Pad to correct colour counts is irrelevant: character check first.
  auto bad_c = validate_facelets_detailed(bad_char);
  ASSERT_FALSE(bad_c.has_value());
  EXPECT_EQ(bad_c.error().kind, ValidationErrorKind::InvalidCharacter);
}

TEST(ValidationTest, RejectsWrongColourCount) {
  std::string f = facelets_of(CubeState::solved());
  f[0] = 'R'; // now U=8, R=10
  auto r = validate_facelets_detailed(f);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().kind, ValidationErrorKind::ColourCount);
  EXPECT_EQ(validate_facelets(f).error(), SolveError::InvalidInput);
}

TEST(ValidationTest, TwistedCornerSpecificError) {
  CubeState s = CubeState::solved();
  s.co[0] = 1; // twist one corner: twist sum = 1
  const std::string f = facelets_of(s);
  auto r = validate_facelets_detailed(f);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().kind, ValidationErrorKind::TwistedCorner);
  EXPECT_EQ(r.error().code, SolveError::InvalidInput);
  EXPECT_FALSE(validate_facelets(f).has_value());
  EXPECT_EQ(validate_facelets(f).error(), SolveError::InvalidInput);
}

TEST(ValidationTest, FlippedEdgeSpecificError) {
  CubeState s = CubeState::solved();
  s.eo[0] = 1; // flip one edge: flip sum = 1
  const std::string f = facelets_of(s);
  auto r = validate_facelets_detailed(f);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().kind, ValidationErrorKind::FlippedEdge);
  EXPECT_FALSE(validate_facelets(f).has_value());
}

TEST(ValidationTest, ParitySwapSpecificError) {
  CubeState s = CubeState::solved();
  // Swap two corners cleanly (orientations stay 0): corner parity flips,
  // edge parity unchanged.
  std::swap(s.cp[0], s.cp[1]);
  const std::string f = facelets_of(s);
  auto r = validate_facelets_detailed(f);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().kind, ValidationErrorKind::Parity);
  EXPECT_FALSE(validate_facelets(f).has_value());

  // Swapping two edges instead also breaks parity equality.
  CubeState e = CubeState::solved();
  std::swap(e.ep[0], e.ep[1]);
  auto r2 = validate_facelets_detailed(facelets_of(e));
  ASSERT_FALSE(r2.has_value());
  EXPECT_EQ(r2.error().kind, ValidationErrorKind::Parity);
}

TEST(ValidationTest, DoubleSwapIsValidParity) {
  // Swapping two corners AND two edges restores parity equality, so the
  // state is parity-valid (it is a legal permutation class).
  CubeState s = CubeState::solved();
  std::swap(s.cp[0], s.cp[1]);
  std::swap(s.ep[0], s.ep[1]);
  EXPECT_TRUE(validate_facelets(facelets_of(s)).has_value());
}
