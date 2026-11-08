#pragma once
// Facelet validation: colour counts, corner twist sum, edge flip sum,
// permutation parity (corner==edge when centres are fixed; total
// corner/edge/centre parity even in general, so slice/rotation states
// validate). Specific errors for twisted corner, flipped edge and
// parity swap are reported via ValidationError.
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "core/interface.hpp"

namespace rubik {

enum class ValidationErrorKind {
  InvalidLength,
  InvalidCharacter,
  ColourCount,
  CentreError,
  DuplicateCubie,
  UnknownCubie,
  TwistedCorner,
  FlippedEdge,
  Parity,
};

struct ValidationError {
  ValidationErrorKind kind = ValidationErrorKind::InvalidLength;
  SolveError code = SolveError::InvalidInput;
  std::string message;
};

// Primary API required by the specification.
std::expected<void, SolveError> validate_facelets(std::string_view facelets);

// Detailed API: distinguishes twisted-corner / flipped-edge / parity
// failures with a specific kind and message.
std::expected<void, ValidationError> validate_facelets_detailed(std::string_view facelets);
std::optional<ValidationError> check_facelets(std::string_view facelets);

} // namespace rubik
