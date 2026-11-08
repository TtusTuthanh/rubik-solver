#include "core/validation.hpp"

#include <format>
#include <map>
#include <string>

#include "core/cube.hpp"

namespace rubik {
namespace {

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

std::optional<ValidationError> check_facelets(std::string_view facelets) {
  auto fail = [](ValidationErrorKind kind, std::string msg) {
    return ValidationError{kind, SolveError::InvalidInput, std::move(msg)};
  };

  if (facelets.size() != 54) {
    return fail(ValidationErrorKind::InvalidLength,
                std::format("facelet string length {} != 54", facelets.size()));
  }
  std::map<char, int> counts;
  for (char c : facelets) {
    if (c != 'U' && c != 'R' && c != 'F' && c != 'D' && c != 'L' && c != 'B') {
      return fail(ValidationErrorKind::InvalidCharacter,
                  std::format("invalid facelet character '{}'", c));
    }
    counts[c]++;
  }
  for (char c : {'U', 'R', 'F', 'D', 'L', 'B'}) {
    if (counts[c] != 9) {
      return fail(ValidationErrorKind::ColourCount,
                  std::format("colour '{}' count {} != 9", c, counts[c]));
    }
  }

  auto parsed = parse_facelets(facelets);
  if (!parsed.has_value()) {
    const std::string msg = parsed.error();
    ValidationErrorKind kind = ValidationErrorKind::UnknownCubie;
    if (msg.find("duplicate") != std::string::npos) {
      kind = ValidationErrorKind::DuplicateCubie;
    } else if (msg.find("centre") != std::string::npos) {
      kind = ValidationErrorKind::CentreError;
    }
    return fail(kind, msg);
  }
  const CubeState& s = parsed.value();

  int twist = 0;
  for (auto v : s.co) twist += v;
  if (twist % 3 != 0) {
    return fail(ValidationErrorKind::TwistedCorner,
                std::format("corner twist sum {} not divisible by 3 (twisted corner)", twist));
  }
  int flip = 0;
  for (auto v : s.eo) flip += v;
  if (flip % 2 != 0) {
    return fail(ValidationErrorKind::FlippedEdge,
                std::format("edge flip sum {} not divisible by 2 (flipped edge)", flip));
  }
  const int corner_parity = parity_of(s.cp.data(), 8);
  const int edge_parity = parity_of(s.ep.data(), 12);
  const int centre_parity = parity_of(s.centers.data(), 6);
  // Total permutation parity must be even. With centres fixed (face
  // moves only) this is exactly corner parity == edge parity; slice
  // moves and whole-cube rotations also move centres, so centre parity
  // must be included for those physical states to validate.
  if ((corner_parity ^ edge_parity ^ centre_parity) != 0) {
    return fail(ValidationErrorKind::Parity,
                "corner/edge/centre permutation parity is odd (parity swap)");
  }
  return std::nullopt;
}

std::expected<void, ValidationError> validate_facelets_detailed(std::string_view facelets) {
  if (auto err = check_facelets(facelets)) {
    return std::unexpected(err.value());
  }
  return {};
}

std::expected<void, SolveError> validate_facelets(std::string_view facelets) {
  if (auto err = check_facelets(facelets)) {
    return std::unexpected(err->code);
  }
  return {};
}

} // namespace rubik
