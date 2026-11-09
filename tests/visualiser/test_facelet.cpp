#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "core/validation.hpp"
#include "visualiser/facelet.hpp"

using namespace rubik;
using namespace rubik::visualiser;

namespace {

constexpr const char* kSolvedStr =
    "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB";

FaceletArray toArray(const std::string& s) {
  FaceletArray a{};
  for (std::size_t i = 0; i < a.size() && i < s.size(); ++i) a[i] = s[i];
  return a;
}

std::string toStr(const FaceletArray& a) { return std::string(a.begin(), a.end()); }

std::vector<Move> must_parse(const std::string& text) {
  auto parsed = parse_moves(text);
  EXPECT_TRUE(parsed.has_value())
      << (parsed.has_value() ? "" : parsed.error());
  if (!parsed.has_value()) return {};
  return parsed.value();
}

bool containsIdx(const std::vector<int>& v, int idx) {
  return std::find(v.begin(), v.end(), idx) != v.end();
}

} // namespace

TEST(FaceletTest, SolvedRoundTrip) {
  CubeState solved = CubeState::solved();
  FaceletArray f = cube_to_facelets(solved);
  EXPECT_EQ(toStr(f), std::string(kSolvedStr));
  EXPECT_EQ(toStr(f), to_facelets(solved));
  auto back = facelets_to_cube(f);
  ASSERT_TRUE(back.has_value())
      << (back.has_value() ? "" : back.error().message);
  EXPECT_EQ(back.value(), solved);
  EXPECT_TRUE(is_solved(back.value()));
  EXPECT_EQ(validate_facelets(f).error, FaceletError::Ok);
}

TEST(FaceletTest, StringOverloadRoundTrip) {
  auto cube = facelets_to_cube(std::string_view(kSolvedStr));
  ASSERT_TRUE(cube.has_value())
      << (cube.has_value() ? "" : cube.error().message);
  EXPECT_EQ(cube.value(), CubeState::solved());
}

TEST(FaceletTest, RandomScrambleRoundTrips) {
  const auto moves = all_face_moves();  // 18 HTM face moves
  ASSERT_EQ(moves.size(), 18u);
  int successes = 0;
  for (unsigned seed = 1; seed <= 25; ++seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> pick(0, moves.size() - 1);
    std::vector<Move> scramble;
    for (int i = 0; i < 25; ++i) scramble.push_back(moves[pick(rng)]);
    CubeState state = apply(CubeState::solved(), scramble);
    ASSERT_FALSE(is_solved(state)) << "seed " << seed;

    FaceletArray f = cube_to_facelets(state);
    EXPECT_EQ(validate_facelets(f).error, FaceletError::Ok) << "seed " << seed;

    auto back = facelets_to_cube(f);
    ASSERT_TRUE(back.has_value())
        << "seed " << seed << ": " << (back.has_value() ? "" : back.error().message);
    EXPECT_EQ(back.value(), state) << "seed " << seed;
    EXPECT_EQ(cube_to_facelets(back.value()), f) << "seed " << seed;
    ++successes;
  }
  EXPECT_EQ(successes, 25);
}

TEST(FaceletTest, ReplayInverseScrambleSolves) {
  const char* scrambles[] = {
      "R U R' U'",
      "F2 D R' L B2 U' F",
      "R U' F D2 L' B R2 U F'",
  };
  for (const char* s : scrambles) {
    auto scramble = must_parse(s);
    CubeState state = apply(CubeState::solved(), scramble);
    ASSERT_FALSE(is_solved(state)) << s;
    CubeState replayed = apply(state, inverse_moves(scramble));
    EXPECT_TRUE(is_solved(replayed)) << s;
    EXPECT_EQ(replayed, CubeState::solved()) << s;
    // Facelet-level replay agrees.
    FaceletArray before = cube_to_facelets(state);
    auto parsed = facelets_to_cube(before);
    ASSERT_TRUE(parsed.has_value()) << s;
    EXPECT_TRUE(is_solved(apply(parsed.value(), inverse_moves(scramble)))) << s;
  }
}

TEST(FaceletTest, InvalidLength) {
  std::string short_str = std::string(kSolvedStr).substr(0, 53);
  FaceletValidation v =
      rubik::visualiser::validate_facelets(std::string_view(short_str));
  EXPECT_EQ(v.error, FaceletError::InvalidLength);
  EXPECT_NE(v.message.find("53"), std::string::npos);
  auto cube = facelets_to_cube(std::string_view(short_str));
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::InvalidLength);
}

TEST(FaceletTest, InvalidCharacter) {
  FaceletArray a = toArray(kSolvedStr);
  a[0] = 'X';
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::InvalidCharacter);
  EXPECT_NE(v.message.find('X'), std::string::npos);
  EXPECT_TRUE(containsIdx(v.offending, 0));
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::InvalidCharacter);
}

TEST(FaceletTest, ColorCount) {
  FaceletArray a = toArray(kSolvedStr);
  a[1] = 'R';  // U: 9 -> 8, R: 9 -> 10; first failing colour is U
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::ColorCount);
  EXPECT_NE(v.message.find('U'), std::string::npos);
  EXPECT_NE(v.message.find('8'), std::string::npos);
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::ColorCount);
}

TEST(FaceletTest, DuplicateCenter) {
  FaceletArray a = toArray(kSolvedStr);
  a[3 * 9 + 4] = 'U';  // D centre -> U (counts balanced below)
  a[2] = 'D';          // a U corner sticker -> D, restoring counts
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::DuplicateCenter);
  EXPECT_NE(v.message.find('U'), std::string::npos);
  EXPECT_TRUE(containsIdx(v.offending, 3 * 9 + 4));
  EXPECT_TRUE(containsIdx(v.offending, 4));
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::DuplicateCenter);
}

TEST(FaceletTest, InvalidCubie) {
  FaceletArray a = toArray(kSolvedStr);
  std::swap(a[0], a[9]);  // URF corner becomes {U,U,F}: impossible cubie
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::InvalidCubie);
  EXPECT_EQ(v.offending.size(), 3u);
  EXPECT_TRUE(containsIdx(v.offending, 8));
  EXPECT_TRUE(containsIdx(v.offending, 9));
  EXPECT_TRUE(containsIdx(v.offending, 20));
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::InvalidCubie);
}

TEST(FaceletTest, DuplicatePiece) {
  // Edge UR (idx 5,10) and UF (idx 7,19) both hold the UF cubie;
// edge DF (idx 28,25) holds (D,R) so colour counts stay at 9.
  FaceletArray a = toArray(kSolvedStr);
  a[1 * 9 + 1] = 'F';  // R2 sticker of UR edge -> F
  a[2 * 9 + 7] = 'R';  // F8 sticker of DF edge -> R (balances counts)
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::DuplicatePiece);
  EXPECT_NE(v.message.find("duplicate"), std::string::npos);
  EXPECT_TRUE(containsIdx(v.offending, 7));
  EXPECT_TRUE(containsIdx(v.offending, 19));
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::DuplicatePiece);
}

TEST(FaceletTest, CornerTwist) {
  // Rotate URF corner stickers (idx 8,9,20) cyclically: twist sum 2.
  FaceletArray a = toArray(kSolvedStr);
  a[8] = 'R';
  a[9] = 'F';
  a[20] = 'U';
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::CornerTwist);
  EXPECT_EQ(v.offending.size(), 3u);
  EXPECT_TRUE(containsIdx(v.offending, 8));
  EXPECT_TRUE(containsIdx(v.offending, 9));
  EXPECT_TRUE(containsIdx(v.offending, 20));
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::CornerTwist);
}

TEST(FaceletTest, EdgeFlip) {
  // Swap UR edge stickers (idx 5,10): flip sum 1.
  FaceletArray a = toArray(kSolvedStr);
  a[5] = 'R';
  a[10] = 'U';
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::EdgeFlip);
  EXPECT_EQ(v.offending.size(), 2u);
  EXPECT_TRUE(containsIdx(v.offending, 5));
  EXPECT_TRUE(containsIdx(v.offending, 10));
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::EdgeFlip);
}

TEST(FaceletTest, Parity) {
  // Swap the UR and UF edge cubies wholesale: parity mismatch.
  FaceletArray a = toArray(kSolvedStr);
  a[1 * 9 + 1] = 'F';  // UR position gets the F sticker
  a[2 * 9 + 1] = 'R';  // UF position gets the R sticker
  FaceletValidation v = validate_facelets(a);
  EXPECT_EQ(v.error, FaceletError::Parity);
  EXPECT_TRUE(v.offending.empty());
  auto cube = facelets_to_cube(a);
  ASSERT_FALSE(cube.has_value());
  EXPECT_EQ(cube.error().error, FaceletError::Parity);
}

TEST(FaceletTest, CoreValidatorAgrees) {
  // Each invalid case is also rejected by rubik::check_facelets
  // (the core validator this module is consistent with).
  std::vector<FaceletArray> invalid;
  {
    FaceletArray a = toArray(kSolvedStr);
    a[1] = 'R';
    invalid.push_back(a);
  }
  {
    FaceletArray a = toArray(kSolvedStr);
    std::swap(a[0], a[9]);
    invalid.push_back(a);
  }
  {
    FaceletArray a = toArray(kSolvedStr);
    a[8] = 'R';
    a[9] = 'F';
    a[20] = 'U';
    invalid.push_back(a);
  }
  for (const auto& a : invalid) {
    EXPECT_TRUE(::rubik::check_facelets(toStr(a)).has_value());
    EXPECT_NE(validate_facelets(a).error, FaceletError::Ok);
  }
  // And a scrambled state is accepted by both.
  auto scramble = must_parse("R U R' F2 D L'");
  FaceletArray good = cube_to_facelets(apply(CubeState::solved(), scramble));
  EXPECT_FALSE(::rubik::check_facelets(toStr(good)).has_value());
  EXPECT_EQ(validate_facelets(good).error, FaceletError::Ok);
}
