#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "reference_sim.hpp"

using namespace rubik;

namespace {

std::vector<Move> must_parse(const std::string& text) {
  auto parsed = parse_moves(text);
  EXPECT_TRUE(parsed.has_value()) << (parsed.has_value() ? "" : parsed.error());
  if (!parsed.has_value()) return {};
  return parsed.value();
}

std::string scrambled_facelets(const std::string& scramble) {
  auto moves = must_parse(scramble);
  return to_facelets(apply(CubeState::solved(), moves));
}

} // namespace

TEST(CubeTest, SolvedFacelets) {
  EXPECT_EQ(to_facelets(CubeState::solved()),
            "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB");
  auto parsed = parse_facelets(to_facelets(CubeState::solved()));
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value(), CubeState::solved());
  EXPECT_TRUE(is_solved(CubeState::solved()));
  EXPECT_TRUE(solved(CubeState::solved()));
  EXPECT_EQ(identity(), CubeState::solved());
}

TEST(CubeTest, FaceletRoundtripScrambles) {
  for (const char* scramble : {"R", "R U R' U'", "F2 D' L B2", "M E S", "x y z",
                               "R U' F2 D L' B U R2", "M2 E' S x' y2 z'"}) {
    const std::string f = scrambled_facelets(scramble);
    ASSERT_EQ(f.size(), 54u);
    auto parsed = parse_facelets(f);
    ASSERT_TRUE(parsed.has_value()) << scramble << ": " << (parsed.has_value() ? "" : parsed.error());
    EXPECT_EQ(to_facelets(parsed.value()), f) << scramble;
  }
}

TEST(CubeTest, FaceletRoundtripRandom) {
  std::mt19937 rng(42);
  auto moves18 = all_face_moves();
  for (int trial = 0; trial < 20; ++trial) {
    std::vector<Move> seq;
    for (int i = 0; i < 25; ++i) seq.push_back(moves18[rng() % moves18.size()]);
    CubeState s = apply(CubeState::solved(), seq);
    auto parsed = parse_facelets(to_facelets(s));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value(), s) << format_moves(seq);
  }
}

TEST(CubeTest, ParseFaceletsRejectsBadInput) {
  EXPECT_FALSE(parse_facelets("").has_value());
  EXPECT_FALSE(parse_facelets("UUU").has_value());
  std::string bad(54, 'U');
  bad[0] = 'X';
  EXPECT_FALSE(parse_facelets(bad).has_value());
}

TEST(MovesTest, IdentityMoves) {
  const CubeState solved = CubeState::solved();
  for (const char* pair : {"R R'", "U U'", "F F'", "D D'", "L L'", "B B'",
                           "M M'", "E E'", "S S'", "x x'", "y y'", "z z'"}) {
    EXPECT_EQ(apply(solved, must_parse(pair)), solved) << pair;
  }
  for (const char* dbl : {"R2 R2", "U2 U2", "F2 F2", "M2 M2", "x2 x2"}) {
    EXPECT_EQ(apply(solved, must_parse(dbl)), solved) << dbl;
  }
  // Four quarter turns.
  EXPECT_EQ(apply(solved, must_parse("R R R R")), solved);
  EXPECT_EQ(apply(solved, must_parse("U U U U")), solved);
}

TEST(MovesTest, NotationParseFormat) {
  auto parsed = parse_moves("R U R' U2 M E' S x y2 z'");
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed.value().size(), 10u);
  EXPECT_EQ(format_moves(parsed.value()), "R U R' U2 M E' S x y2 z'");
  EXPECT_EQ(format_moves({}), "");
  auto empty = parse_moves("");
  ASSERT_TRUE(empty.has_value());
  EXPECT_TRUE(empty.value().empty());
  EXPECT_FALSE(parse_moves("R Q").has_value());
  EXPECT_FALSE(parse_moves("RR").has_value());
  EXPECT_FALSE(parse_moves("r").has_value());
  EXPECT_FALSE(parse_moves("R3").has_value());
  EXPECT_EQ(all_face_moves().size(), 18u);
  EXPECT_EQ(inverse_move(Move{"R"}), Move{"R'"});
  EXPECT_EQ(inverse_move(Move{"R'"}), Move{"R"});
  EXPECT_EQ(inverse_move(Move{"R2"}), Move{"R2"});
}

TEST(MovesTest, ScrambleThenInverseYieldsSolved) {
  const CubeState solved = CubeState::solved();
  auto seq = must_parse("R U R' F2 D L' B2 M E S x y");
  CubeState scrambled = apply(solved, seq);
  EXPECT_NE(scrambled, solved);
  EXPECT_EQ(apply(scrambled, inverse_moves(seq)), solved);
  // Single-move helpers.
  CubeState s = apply(solved, Move{"R"});
  EXPECT_EQ(apply(s, inverse_move(Move{"R"})), solved);
  EXPECT_EQ(apply_sequence(solved, seq), scrambled);
}

TEST(MovesTest, Compose) {
  CubeState r = apply(CubeState::solved(), Move{"R"});
  CubeState rp = apply(CubeState::solved(), Move{"R'"});
  EXPECT_EQ(compose(r, rp), CubeState::solved());
  EXPECT_EQ(compose(r, r), apply(CubeState::solved(), must_parse("R2")));
  CubeState u = apply(CubeState::solved(), Move{"U"});
  EXPECT_EQ(compose(r, u), apply(CubeState::solved(), must_parse("R U")));
  EXPECT_EQ(compose(CubeState::solved(), r), r);
  EXPECT_EQ(compose(r, CubeState::solved()), r);
}

TEST(ReferenceSimTest, AgreesWithCoreOnSolved) {
  const std::string solved_f = refsim::solved_facelets();
  for (const auto& m : all_face_moves()) {
    EXPECT_EQ(scrambled_facelets(m.token), refsim::apply_token(solved_f, m.token)) << m.token;
  }
  for (const char* tok : {"M", "M'", "M2", "E", "E'", "E2", "S", "S'", "S2",
                          "x", "x'", "x2", "y", "y'", "y2", "z", "z'", "z2"}) {
    EXPECT_EQ(scrambled_facelets(tok), refsim::apply_token(solved_f, tok)) << tok;
  }
}

TEST(ReferenceSimTest, AgreesWithCoreOnScrambles) {
  const std::vector<std::string> scrambles = {
      "R U R' U'", "F2 D' L B2 U R", "M E S x y z", "R2 U2 F2 D2 L2 B2",
      "U R' F' D2 L B U2 R"};
  for (const auto& scramble : scrambles) {
    auto moves = must_parse(scramble);
    std::vector<std::string> tokens;
    for (const auto& m : moves) tokens.push_back(m.token);
    const std::string core_f = to_facelets(apply(CubeState::solved(), moves));
    const std::string ref_f = refsim::apply_sequence(refsim::solved_facelets(), tokens);
    EXPECT_EQ(core_f, ref_f) << scramble;
  }
}

TEST(ReferenceSimTest, ScrambleThenInverseYieldsSolved) {
  const std::vector<std::string> tokens = {"R", "U", "R'", "F2", "D", "L'", "M", "E"};
  const std::string scrambled = refsim::apply_sequence(refsim::solved_facelets(), tokens);
  const std::string restored =
      refsim::apply_sequence(scrambled, refsim::inverse_sequence(tokens));
  EXPECT_EQ(restored, refsim::solved_facelets());
  EXPECT_NE(scrambled, refsim::solved_facelets());
}

TEST(ReferenceSimTest, CoreScrambleVerifiedByReference) {
  // Reference simulator verifies core solutions: core scramble applied
  // in reference must be undone by the inverse sequence in reference.
  const std::vector<std::string> tokens = {"R", "U'", "F", "D2", "L", "B'"};
  auto moves = must_parse("R U' F D2 L B'");
  const std::string ref_scrambled =
      refsim::apply_sequence(refsim::solved_facelets(), tokens);
  EXPECT_EQ(ref_scrambled, to_facelets(apply(CubeState::solved(), moves)));
  EXPECT_EQ(refsim::apply_sequence(ref_scrambled, refsim::inverse_sequence(tokens)),
            refsim::solved_facelets());
}
