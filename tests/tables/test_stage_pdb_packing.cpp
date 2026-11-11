// Tests for the nibble-packed stage-PDB format.
//
// These cover BUG-01: the packer used to OR the raw byte distance into a
// 4-bit field, so a distance >= 16 spilled bit 4 into the *neighbouring*
// entry and corrupted it.  The format's usable range is 0..14 (0x0F is the
// "unreached" sentinel), the builder now caps its BFS at that range, and a
// pattern whose true diameter does not fit is rejected loudly instead of
// being silently truncated.

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/cube.hpp"
#include "core/moves.hpp"
#include "core/pdb.hpp"
#include "tables/stage_config.hpp"
#include "tables/stage_pdb.hpp"

using rubik::CubeState;
using rubik::tables::buildNibbleDistanceTable;
using rubik::tables::buildStagePdb;
using rubik::tables::kNibbleMaxDistance;
using rubik::tables::kNibbleUnreached;
using rubik::tables::NibbleRangeExceeded;
using rubik::tables::packNibbleDistances;
using rubik::tables::Pattern;
using rubik::tables::StageConfig;
using rubik::tables::unpackNibbleDistance;

namespace {

// Decode a whole packed table.
std::vector<int> decodeAll(const std::vector<std::uint8_t>& packed,
                           std::size_t num_entries) {
    std::vector<int> out(num_entries, 0);
    for (std::size_t i = 0; i < num_entries; ++i) {
        out[i] = unpackNibbleDistance(packed.data(), i);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The packer itself
// ---------------------------------------------------------------------------

TEST(NibblePacking, SentinelDecodesToZero) {
    // 0x0F means "not reached"; it must decode to 0 so that a lower bound is
    // never violated.
    const std::uint8_t packed[1] = {0xFF};
    EXPECT_EQ(unpackNibbleDistance(packed, 0), 0);
    EXPECT_EQ(unpackNibbleDistance(packed, 1), 0);

    // A table of pure sentinels decodes to all zeros.
    const std::vector<std::uint8_t> all_sentinel(4, 0xFF);
    for (int v : decodeAll(all_sentinel, 8)) {
        EXPECT_EQ(v, 0);
    }
}

TEST(NibblePacking, ExactDistancesRoundTrip) {
    // Every representable distance (0..14) must survive a pack/unpack cycle
    // unchanged.
    EXPECT_EQ(static_cast<int>(kNibbleMaxDistance), 14);
    EXPECT_EQ(static_cast<int>(kNibbleUnreached), 0x0F);
    std::vector<std::uint8_t> dist;
    for (int v = 0; v <= static_cast<int>(kNibbleMaxDistance); ++v) {
        dist.push_back(static_cast<std::uint8_t>(v));
    }
    const auto packed = packNibbleDistances(dist);
    const auto decoded = decodeAll(packed, dist.size());
    for (std::size_t i = 0; i < dist.size(); ++i) {
        EXPECT_EQ(decoded[i], static_cast<int>(dist[i]))
            << "entry " << i << " must round-trip exactly";
    }
}

TEST(NibblePacking, NoEntryContaminatesItsNeighbour) {
    // The regression test for BUG-01: a distance whose bit 4 is set must not
    // spill into the neighbouring nibble.
    //
    // 0xFE has bit 4 set.  Under the old packer, the even entry wrote
    // 0xFE into the byte, so the *odd* neighbour read back 0x0F (the
    // sentinel) instead of its own value.
    const std::vector<std::uint8_t> dist = {
        0xFE,  // even entry: an out-of-range value that must be masked
        0x01,  // odd neighbour: must survive untouched
        0x02,  // even entry of the next byte
        0x03,  // odd neighbour of the next byte
    };
    const auto packed = packNibbleDistances(dist);
    EXPECT_EQ(packed.size(), 2u);

    // Entry 0 keeps its low four bits (0x0E); bit 4 is dropped, so it can
    // never reach the odd neighbour.
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 0), 0x0E);
    // Entry 1 is NOT contaminated: it still reads 0x01, not 0x0F.
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 1), 0x01);
    // The second byte is unaffected too.
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 2), 0x02);
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 3), 0x03);
}

TEST(NibblePacking, SentinelEntriesDoNotDisturbNeighbours) {
    const std::vector<std::uint8_t> dist = {
        rubik::core::kDbSentinel,  // unreached
        0x07,
        0x0E,
        rubik::core::kDbSentinel,
    };
    const auto packed = packNibbleDistances(dist);
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 0), 0);
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 1), 0x07);
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 2), 0x0E);
    EXPECT_EQ(unpackNibbleDistance(packed.data(), 3), 0);
}

// ---------------------------------------------------------------------------
// The BFS guard
// ---------------------------------------------------------------------------

TEST(NibbleRangeGuard, AcceptsExactlyFourteen) {
    // A chain of 15 states has a true diameter of exactly 14 -- the largest
    // value the nibble format can represent.  It must build successfully and
    // store the distance exactly.
    // `predecessors(cur)` must yield the states that reach `cur` in one
    // move, i.e. the BACKWARD neighbours of the goal.
    constexpr std::size_t kN = 15;
    std::vector<std::vector<std::uint32_t>> preds(kN);
    for (std::size_t i = 0; i + 1 < kN; ++i) {
        preds[i].push_back(static_cast<std::uint32_t>(i + 1));
    }
    const std::vector<std::uint32_t> goals = {0};

    const auto packed = buildNibbleDistanceTable(
        kN, goals, [&](std::uint32_t idx) -> const std::vector<std::uint32_t>& {
            return preds[idx];
        });

    const auto decoded = decodeAll(packed, kN);
    for (std::size_t i = 0; i < kN; ++i) {
        EXPECT_EQ(decoded[i], static_cast<int>(i)) << "entry " << i;
    }
}

TEST(NibbleRangeGuard, RejectsDiameterAboveFourteen) {
    // A chain of 30 states has a true diameter of 29.  The builder must
    // refuse loudly -- never silently truncate to 14 and store wrong values.
    constexpr std::size_t kN = 30;
    std::vector<std::vector<std::uint32_t>> preds(kN);
    for (std::size_t i = 0; i + 1 < kN; ++i) {
        preds[i].push_back(static_cast<std::uint32_t>(i + 1));
    }
    const std::vector<std::uint32_t> goals = {0};

    EXPECT_THROW(
        {
            (void)buildNibbleDistanceTable(
                kN, goals,
                [&](std::uint32_t idx) -> const std::vector<std::uint32_t>& {
                    return preds[idx];
                });
        },
        NibbleRangeExceeded);
}

TEST(NibbleRangeGuard, UnreachableStatesDecodeToZero) {
    // A graph whose states 5..7 are not connected to the goal: they must
    // decode to 0 (an unreached state has no lower-bound violation).
    constexpr std::size_t kN = 8;
    std::vector<std::vector<std::uint32_t>> preds(kN);
    for (std::size_t i = 0; i < 4; ++i) {
        preds[i].push_back(static_cast<std::uint32_t>(i + 1));
    }
    const std::vector<std::uint32_t> goals = {0};

    const auto packed = buildNibbleDistanceTable(
        kN, goals, [&](std::uint32_t idx) -> const std::vector<std::uint32_t>& {
            return preds[idx];
        });
    const auto decoded = decodeAll(packed, kN);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(decoded[i], static_cast<int>(i));
    }
    for (std::size_t i = 5; i < kN; ++i) {
        EXPECT_EQ(decoded[i], 0) << "unreachable entry " << i;
    }
}

// ---------------------------------------------------------------------------
// End-to-end buildStagePdb over a real cube
// ---------------------------------------------------------------------------

namespace {

// Corner-position permutations of the 18 face moves, derived from the
// repository's own move definitions so that the test cannot drift from the
// solver's move semantics.
const std::array<std::array<std::uint8_t, 8>, 18>& cornerMovePermutations() {
    static const std::array<std::array<std::uint8_t, 8>, 18> perms = [] {
        std::array<std::array<std::uint8_t, 8>, 18> out{};
        const auto moves = rubik::all_face_moves();
        for (std::size_t i = 0; i < 18; ++i) {
            const CubeState after = rubik::apply(CubeState::solved(), moves[i]);
            out[i] = after.cp;  // solved.cp is the identity, so after.cp is the permutation
        }
        return out;
    }();
    return perms;
}

// The pattern tracks the POSITIONS of the first kTracked cubies.
constexpr int kTracked = 5;
constexpr std::size_t kPatternStates = 6720;  // 8 * 7 * 6 * 5 * 4

// Position of every cubie: pos[c] = the corner position holding cubie c.
std::array<std::uint8_t, 8> cubiePositions(const std::array<std::uint8_t, 8>& cp) {
    std::array<std::uint8_t, 8> pos{};
    for (int p = 0; p < 8; ++p) {
        pos[cp[static_cast<std::size_t>(p)]] = static_cast<std::uint8_t>(p);
    }
    return pos;
}

// Rank of the tracked tuple inside the 8P5 orderings.
std::uint32_t rankTracked(const std::array<std::uint8_t, 8>& pos) {
    std::array<std::uint8_t, 8> avail{0, 1, 2, 3, 4, 5, 6, 7};
    std::uint32_t rank = 0;
    for (int i = 0; i < kTracked; ++i) {
        const std::uint8_t want = pos[static_cast<std::size_t>(i)];
        int c = 0;
        while (avail[static_cast<std::size_t>(c)] != want) ++c;
        rank = rank * static_cast<std::uint32_t>(8 - i) + static_cast<std::uint32_t>(c);
        for (int j = c; j < 8 - i - 1; ++j) {
            avail[static_cast<std::size_t>(j)] = avail[static_cast<std::size_t>(j + 1)];
        }
    }
    return rank;
}

// Inverse of `rankTracked`.
std::array<std::uint8_t, 8> unrankTracked(std::uint32_t rank) {
    // Extract the Lehmer digits, least significant first.
    std::array<int, kTracked> digit{};
    std::uint32_t rest = rank;
    for (int i = kTracked - 1; i >= 0; --i) {
        const std::uint32_t div = static_cast<std::uint32_t>(8 - i);
        digit[static_cast<std::size_t>(i)] = static_cast<int>(rest % div);
        rest /= div;
    }
    // Replay the ranking process forwards.
    std::array<std::uint8_t, 8> avail{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<std::uint8_t, 8> pos{};
    for (int i = 0; i < kTracked; ++i) {
        const int ci = digit[static_cast<std::size_t>(i)];
        pos[static_cast<std::size_t>(i)] = avail[static_cast<std::size_t>(ci)];
        for (int j = ci; j < 8 - i - 1; ++j) {
            avail[static_cast<std::size_t>(j)] = avail[static_cast<std::size_t>(j + 1)];
        }
    }
    return pos;
}

// Build a cube state whose tracked cubies sit at the given positions.  The
// untracked cubies fill the remaining positions in a canonical order; they do
// not influence the pattern index.
CubeState stateFromTrackedPositions(const std::array<std::uint8_t, 8>& pos) {
    CubeState s = CubeState::solved();
    std::array<bool, 8> taken{};
    for (int i = 0; i < kTracked; ++i) {
        const auto p = static_cast<std::size_t>(pos[static_cast<std::size_t>(i)]);
        s.cp[p] = static_cast<std::uint8_t>(i);
        taken[p] = true;
    }
    std::uint8_t next = static_cast<std::uint8_t>(kTracked);
    for (std::size_t p = 0; p < 8; ++p) {
        if (!taken[p]) {
            s.cp[p] = next++;
        }
    }
    return s;
}

// The chain lengths used by the synthetic "diameter" patterns.  They live at
// namespace scope so that the lambdas below can use them without capturing.
constexpr int kChain30 = 30;
constexpr int kChain15 = 15;

}  // namespace

TEST(StagePdbEndToEnd, StoredDistancesAreExact) {
    // Build a real stage PDB over the 18 face moves and check every stored
    // entry against an independently computed true distance.
    StageConfig config;
    config.stage_name = "corner_positions";
    config.patterns = {Pattern{"corner_pos", {}, {}, true, kPatternStates}};
    config.allowed_moves = rubik::tables::all_18_face_move_indices();
    config.goal_predicate = [](const CubeState&) { return true; };
    config.goal_states = {CubeState::solved()};

    const auto& perms = cornerMovePermutations();

    auto encode_fn = [&](const CubeState& s) -> std::uint32_t {
        return rankTracked(cubiePositions(s.cp));
    };
    auto decode_fn = [&](std::uint32_t idx) -> CubeState {
        return stateFromTrackedPositions(unrankTracked(idx));
    };
    auto apply_fn = [&](const CubeState& s, int move) -> CubeState {
        CubeState out = s;
        const auto& perm = perms[static_cast<std::size_t>(move)];
        for (int p = 0; p < 8; ++p) {
            out.cp[static_cast<std::size_t>(p)] =
                s.cp[perm[static_cast<std::size_t>(p)]];
        }
        return out;
    };

    // Independent forward BFS from the goal over the same move set.
    // (The move set is closed under inverses, so the forward distance from the
    // goal equals the backward distance to the goal.)
    constexpr std::uint32_t kUnreached = 0xFFFFFFFFu;
    std::vector<std::uint32_t> true_dist(kPatternStates, kUnreached);
    std::vector<std::uint32_t> queue;
    queue.reserve(kPatternStates);
    true_dist[0] = 0;
    queue.push_back(0);
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::uint32_t cur = queue[head];
        const auto pos = unrankTracked(cur);
        const CubeState s = stateFromTrackedPositions(pos);
        for (std::size_t m = 0; m < 18; ++m) {
            const auto& perm = perms[m];
            CubeState next = s;
            for (int p = 0; p < 8; ++p) {
                next.cp[static_cast<std::size_t>(p)] =
                    s.cp[perm[static_cast<std::size_t>(p)]];
            }
            const std::uint32_t nidx = rankTracked(cubiePositions(next.cp));
            if (true_dist[nidx] == kUnreached) {
                true_dist[nidx] = true_dist[cur] + 1;
                queue.push_back(nidx);
            }
        }
    }

    int max_true = 0;
    for (std::size_t i = 0; i < kPatternStates; ++i) {
        if (true_dist[i] != kUnreached) {
            max_true = std::max(max_true, static_cast<int>(true_dist[i]));
        }
    }
    EXPECT_GE(max_true, 2) << "the pattern must exercise more than one move";

    bool refused = false;
    std::vector<std::uint8_t> packed;
    try {
        packed = buildStagePdb(config, 0, encode_fn, apply_fn, decode_fn);
    } catch (const NibbleRangeExceeded&) {
        refused = true;
    }

    if (refused) {
        // The builder may only refuse a pattern it genuinely cannot
        // represent exactly.
        EXPECT_GT(max_true, static_cast<int>(kNibbleMaxDistance))
            << "the builder refused a pattern whose diameter fits the format";
        return;
    }

    ASSERT_EQ(packed.size(), (kPatternStates + 1) / 2);

    // Every stored entry must be EXACT, and never above the truth.
    for (std::size_t i = 0; i < kPatternStates; ++i) {
        const int stored = unpackNibbleDistance(packed.data(), i);
        if (true_dist[i] == kUnreached) {
            EXPECT_EQ(stored, 0) << "unreachable entry " << i << " must decode to 0";
        } else {
            EXPECT_EQ(stored, static_cast<int>(true_dist[i]))
                << "entry " << i << " must store the exact distance";
            EXPECT_LE(stored, static_cast<int>(true_dist[i]))
                << "entry " << i << " must never exceed the true distance";
        }
    }
}

TEST(StagePdbEndToEnd, RefusesPatternWhoseDiameterExceedsTheFormat) {
    // A synthetic pattern whose move graph is a 30-state chain: its true
    // diameter (29) cannot be represented in a nibble, so the builder must
    // throw instead of silently truncating.
    constexpr std::size_t kChain = 30;
    StageConfig config;
    config.stage_name = "chain";
    config.patterns = {Pattern{"chain", {}, {}, true, kChain}};
    config.allowed_moves = {0, 1};
    config.goal_predicate = [](const CubeState&) { return true; };
    config.goal_states = {CubeState::solved()};

    // The chain position is carried in cp[0].
    auto encode_fn = [](const CubeState& s) -> std::uint32_t {
        return static_cast<std::uint32_t>(s.cp[0]);
    };
    auto decode_fn = [](std::uint32_t idx) -> CubeState {
        CubeState s = CubeState::solved();
        s.cp[0] = static_cast<std::uint8_t>(idx);
        return s;
    };
    auto apply_fn = [](const CubeState& s, int move) -> CubeState {
        CubeState out = s;
        const int v = static_cast<int>(s.cp[0]);
        if (move == 0) {
            out.cp[0] = static_cast<std::uint8_t>(v + 1 < kChain30 ? v + 1 : v);
        } else {
            out.cp[0] = static_cast<std::uint8_t>(v > 0 ? v - 1 : v);
        }
        return out;
    };

    EXPECT_THROW(
        (void)buildStagePdb(config, 0, encode_fn, apply_fn, decode_fn),
        NibbleRangeExceeded);
}

TEST(StagePdbEndToEnd, AcceptsPatternWhoseDiameterIsExactlyFourteen) {
    // The same synthetic pattern, shortened to 15 states: diameter 14, which
    // the nibble format represents exactly.
    constexpr std::size_t kChain = 15;
    StageConfig config;
    config.stage_name = "chain";
    config.patterns = {Pattern{"chain", {}, {}, true, kChain}};
    config.allowed_moves = {0, 1};
    config.goal_predicate = [](const CubeState&) { return true; };
    config.goal_states = {CubeState::solved()};

    auto encode_fn = [](const CubeState& s) -> std::uint32_t {
        return static_cast<std::uint32_t>(s.cp[0]);
    };
    auto decode_fn = [](std::uint32_t idx) -> CubeState {
        CubeState s = CubeState::solved();
        s.cp[0] = static_cast<std::uint8_t>(idx);
        return s;
    };
    auto apply_fn = [](const CubeState& s, int move) -> CubeState {
        CubeState out = s;
        const int v = static_cast<int>(s.cp[0]);
        if (move == 0) {
            out.cp[0] = static_cast<std::uint8_t>(v + 1 < kChain15 ? v + 1 : v);
        } else {
            out.cp[0] = static_cast<std::uint8_t>(v > 0 ? v - 1 : v);
        }
        return out;
    };

    const auto packed = buildStagePdb(config, 0, encode_fn, apply_fn, decode_fn);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kChain15); ++i) {
        EXPECT_EQ(unpackNibbleDistance(packed.data(), i), static_cast<int>(i))
            << "entry " << i;
    }
}
