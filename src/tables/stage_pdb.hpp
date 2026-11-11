#pragma once
// Generic stage-PDB builder: backward BFS from stage goal using stage move set.
// Builds nibble-packed (4 bits/entry) distance tables for any pattern coordinate system.
//
// The nibble format stores one distance in four bits and reserves 0x0F as the
// "unreached" sentinel, so the representable range is 0..14.  See
// `tables/stage_config.hpp` for the format's single source of truth.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/pdb.hpp"
#include "tables/stage_config.hpp"

namespace rubik::tables {

// Thrown when a pattern's true maximum distance cannot be represented exactly
// by the nibble-packed format (i.e. it exceeds `kNibbleMaxDistance` = 14).
//
// The nibble format stores one distance in four bits and reserves 0x0F as the
// "not reached" sentinel, so the representable range is 0..14.  A pattern
// whose true diameter is larger must use the byte-wide format
// (`core::bfsDb` / `core::pdbLookup`, sentinel 255, range 0..254) instead.
//
// Failing loudly is mandatory: silently truncating would store a WRONG
// distance, and a wrong distance that decodes above the true one makes the
// heuristic inadmissible.
class NibbleRangeExceeded : public std::runtime_error {
public:
    explicit NibbleRangeExceeded(const std::string& message)
        : std::runtime_error(message) {}
};

// Coordinate move table for O(1) index updates during BFS and search.
// Maps (pattern_index, move) -> new_pattern_index.
// Built per pattern per stage move set.
template <typename IndexType>
class CoordinateMoveTable {
public:
    using Index = IndexType;

    CoordinateMoveTable() = default;
    CoordinateMoveTable(std::size_t num_states, std::size_t num_moves)
        : num_states_(num_states), num_moves_(num_moves), table_(num_states * num_moves) {}

    // Build the table by applying each move to each state index.
    // `decode_fn`: index -> pattern state (for applying move)
    // `encode_fn`: pattern state -> index (for storing result)
    // `apply_move_fn`: (pattern_state, move) -> new_pattern_state
    template <typename PatternState, typename DecodeFn, typename EncodeFn, typename ApplyFn>
    void build(DecodeFn&& decode_fn, EncodeFn&& encode_fn, ApplyFn&& apply_move_fn,
               const std::vector<int>& allowed_moves) {
        table_.resize(num_states_ * num_moves_);
        for (Index idx = 0; idx < static_cast<Index>(num_states_); ++idx) {
            PatternState state = decode_fn(idx);
            for (std::size_t mi = 0; mi < num_moves_; ++mi) {
                int move = allowed_moves[mi];
                PatternState next_state = apply_move_fn(state, move);
                Index next_idx = encode_fn(next_state);
                table_[idx * num_moves_ + mi] = next_idx;
            }
        }
    }

    // O(1) successor lookup.
    [[nodiscard]] Index move(Index idx, std::size_t move_idx) const noexcept {
        if (idx >= num_states_ || move_idx >= num_moves_) return 0;
        return table_[idx * num_moves_ + move_idx];
    }

    [[nodiscard]] std::size_t numStates() const noexcept { return num_states_; }
    [[nodiscard]] std::size_t numMoves() const noexcept { return num_moves_; }
    [[nodiscard]] const std::vector<Index>& data() const noexcept { return table_; }

private:
    std::size_t num_states_ = 0;
    std::size_t num_moves_ = 0;
    std::vector<Index> table_;
};

// Backward BFS over an explicit predecessor relation, producing a
// nibble-packed distance table.
//
// `predecessors(idx)` must yield the predecessor indices of `idx` (the
// states that reach `idx` in one allowed move).
//
// The BFS is capped at `kNibbleMaxDistance` (14).  If any state is reachable
// at a greater distance -- i.e. the pattern's true diameter exceeds what the
// nibble format can represent -- the function throws `NibbleRangeExceeded`
// instead of truncating.  A truncated distance is not merely a weaker
// heuristic: the packer would spill bit 4 into the neighbouring entry and
// corrupt it.
//
// States that are not reachable from any goal keep the nibble sentinel
// (0x0F) and decode to 0, which is a lower bound and therefore admissible.
template <typename PredFn>
[[nodiscard]] std::vector<std::uint8_t> buildNibbleDistanceTable(
    std::size_t num_states,
    const std::vector<std::uint32_t>& goal_indices,
    PredFn&& predecessors) {
    std::vector<std::uint8_t> dist(num_states, core::kDbSentinel);
    std::vector<std::uint32_t> queue;
    queue.reserve(num_states / 10 + 1);

    for (std::uint32_t goal_idx : goal_indices) {
        if (goal_idx < num_states && dist[goal_idx] == core::kDbSentinel) {
            dist[goal_idx] = 0;
            queue.push_back(goal_idx);
        }
    }

    bool overflow = false;
    std::size_t qhead = 0;
    while (qhead < queue.size()) {
        const std::uint32_t cur = queue[qhead++];
        const std::uint8_t d = dist[cur];
        if (d >= kNibbleMaxDistance) {
            // At the cap.  Any predecessor that is still unvisited would be at
            // distance d+1 = 15, which the nibble format cannot represent
            // exactly.  Detect it here so the builder can fail loudly rather
            // than silently truncating.
            for (const std::uint32_t prev : predecessors(cur)) {
                if (prev < num_states && dist[prev] == core::kDbSentinel) {
                    overflow = true;
                    break;
                }
            }
            continue;
        }
        for (const std::uint32_t prev : predecessors(cur)) {
            if (prev >= num_states) continue;
            if (dist[prev] == core::kDbSentinel) {
                dist[prev] = static_cast<std::uint8_t>(d + 1);
                queue.push_back(prev);
            }
        }
    }

    if (overflow) {
        throw NibbleRangeExceeded(
            "nibble-packed pattern database: the true maximum distance exceeds " +
            std::to_string(static_cast<int>(kNibbleMaxDistance)) +
            " and cannot be represented exactly; use a byte-wide pattern "
            "database (core/pdb.hpp) instead");
    }

    return packNibbleDistances(dist);
}

// Build a single pattern database via backward BFS from goal state(s).
//
// Parameters:
//   config: StageConfig containing pattern definition, allowed moves, goal states
//   pattern_idx: Index into config.patterns[] for the pattern to build
//   encode_fn: CubeState -> pattern index (for the specific pattern)
//   apply_fn: (CubeState, move) -> CubeState (full cube move application)
//   decode_fn: pattern index -> CubeState (for BFS expansion; only needed for goal states)
//
// Returns: Nibble-packed distance table (vector<uint8_t>, size = ceil(num_states/2)).
// Sentinel 0xF = unreached; maps to 0 at lookup time (admissible).
//
// THROWS `NibbleRangeExceeded` when the pattern's true maximum distance
// exceeds `kNibbleMaxDistance` (14): such a pattern cannot be represented
// exactly by the nibble format and MUST NOT be silently truncated.
template <typename EncodeFn, typename ApplyFn, typename DecodeFn>
[[nodiscard]] std::vector<std::uint8_t> buildStagePdb(
    const StageConfig& config,
    std::size_t pattern_idx,
    EncodeFn&& encode_fn,        // CubeState -> pattern index
    ApplyFn&& apply_fn,          // (CubeState, move) -> CubeState
    DecodeFn&& decode_fn) {      // pattern index -> CubeState (for goal expansion)

    const auto& pattern = config.patterns[pattern_idx];
    const std::size_t num_states = pattern.num_states;
    const std::vector<int>& allowed_moves = config.allowed_moves;

    // Compute goal indices (multi-source BFS).
    std::vector<std::uint32_t> goal_indices;
    goal_indices.reserve(config.goal_states.size());
    for (const auto& goal_state : config.goal_states) {
        std::uint32_t idx = encode_fn(goal_state);
        if (idx < num_states) goal_indices.push_back(idx);
    }

    // Build coordinate move table for this pattern and move set.
    // We need a pattern state type; use CubeState for generality (decode_fn returns full state).
    // For efficiency, solvers should provide specialized pattern state types.
    CoordinateMoveTable<std::uint32_t> move_table(num_states, allowed_moves.size());

    // Build move table by applying moves to all states.
    // Note: This is the expensive part. For large tables, we can optimize by only
    // building the move table for states actually visited during BFS (on-demand),
    // but prebuilding gives O(1) per edge during BFS.
    // NOTE: `PatternState` cannot be deduced from the arguments (it only
    // appears in the body), so it is stated explicitly.
    move_table.build<CubeState>(
        [&](std::uint32_t idx) -> CubeState { return decode_fn(idx); },
        [&](const CubeState& s) -> std::uint32_t { return encode_fn(s); },
        [&](const CubeState& s, int move) -> CubeState { return apply_fn(s, move); },
        allowed_moves
    );

    // For backward BFS we need the inverse move table. Build it:
    std::vector<std::vector<std::uint32_t>> inverse_moves(num_states);
    for (std::uint32_t idx = 0; idx < num_states; ++idx) {
        for (std::size_t mi = 0; mi < allowed_moves.size(); ++mi) {
            std::uint32_t next = move_table.move(idx, mi);
            if (next < num_states) {
                inverse_moves[next].push_back(idx);
            }
        }
    }

    // Run the backward BFS from the goal indices using the inverse moves.
    // The BFS is capped at `kNibbleMaxDistance` and throws when the pattern's
    // true diameter does not fit the nibble format.
    return buildNibbleDistanceTable(
        num_states, goal_indices,
        [&](std::uint32_t idx) -> const std::vector<std::uint32_t>& {
            return inverse_moves[idx];
        });
}

// Helper: write a nibble-packed PDB to a file (raw binary, no header).
[[nodiscard]] bool writePdbFile(const std::filesystem::path& path,
                                const std::vector<std::uint8_t>& packed_data);

// Helper: read a nibble-packed PDB from a file (raw binary, no header).
// Returns empty vector on failure.
[[nodiscard]] std::vector<std::uint8_t> readPdbFile(const std::filesystem::path& path,
                                                    std::size_t expected_entries);

}  // namespace rubik::tables
