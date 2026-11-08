#pragma once
// Move notation: 18 face moves (U,R,F,D,L,B x X/X'/X2), slice moves
// M/E/S (with ' and 2) and whole-cube rotations x/y/z (with ' and 2).
// Each token counts as 1 move. Tokens are whitespace-separated.
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "core/cube.hpp"

namespace rubik {

struct Move {
  std::string token; // e.g. "R", "R'", "R2", "M", "x", "y2"
  bool operator==(const Move&) const = default;
  bool operator!=(const Move&) const = default;
};

bool is_valid_move_token(std::string_view token);
std::expected<std::vector<Move>, std::string> parse_moves(std::string_view text);
std::string format_moves(const std::vector<Move>& moves);
std::vector<Move> all_face_moves(); // the 18 face moves
Move inverse_move(const Move& move);
std::vector<Move> inverse_moves(const std::vector<Move>& moves);

CubeState apply(const CubeState& state, const Move& move);
CubeState apply(const CubeState& state, const std::vector<Move>& moves);
inline CubeState apply_sequence(const CubeState& state, const std::vector<Move>& moves) {
  return apply(state, moves);
}

} // namespace rubik
