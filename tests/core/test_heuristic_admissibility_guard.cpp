// Tests for the structural admissibility gate of the shared heuristic
// combinator (BUG-02 / F-04 / F-06b / F-11 / F-12).
//
// A DisjointGroup may be combined with SUM only when
//   1. it is EXPLICITLY opted in,
//   2. its constituents track pairwise-disjoint pieces, and
//   3. its constituents' MOVE-SUPPORT sets are disjoint over the stage's own
//      allowed move set (Korf & Felner 2002).
//
// Piece-disjointness alone is NOT sufficient: one face turn advances several
// piece-disjoint groups at once, so h(G1) + h(G2) can exceed the true joint
// distance.  MAX is the default and the universal fallback.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/cube.hpp"
#include "core/heuristic.hpp"
#include "tables/stage_config.hpp"

using rubik::core::PdbLookup;
using rubik::core::StageHeuristic;
using rubik::tables::DisjointGroup;
using rubik::tables::Pattern;
using rubik::CubeState;

namespace {

// Storage that keeps the mock tables alive for the lifetime of the test.
std::vector<std::vector<std::uint8_t>>& mockStorage() {
    static std::vector<std::vector<std::uint8_t>> storage;
    return storage;
}

// A nibble-packed table whose entries all decode to `value`.
PdbLookup makeMockPdb(const std::string& name, int value,
                      std::size_t num_entries = 1000) {
    std::vector<std::uint8_t> data((num_entries + 1) / 2, 0);
    const std::uint8_t v = static_cast<std::uint8_t>(value & 0x0F);
    for (std::size_t i = 0; i < num_entries; ++i) {
        const std::size_t b = i >> 1;
        if ((i & 1) == 0) {
            data[b] = static_cast<std::uint8_t>((data[b] & 0xF0u) | v);
        } else {
            data[b] = static_cast<std::uint8_t>((data[b] & 0x0Fu) | (v << 4));
        }
    }
    mockStorage().push_back(std::move(data));
    const std::uint8_t* ptr = mockStorage().back().data();
    return PdbLookup{
        .name = name,
        .data = ptr,
        .num_entries = num_entries,
        .index_fn = [](const CubeState&) -> std::uint32_t { return 0; },
    };
}

// The {U, R, L} alphabet used by ZZ's 9-move stages.
const std::vector<int>& zzAlphabet() {
    static const std::vector<int> moves = {0, 1, 2, 3, 4, 5, 12, 13, 14};
    return moves;
}

}  // namespace

// ---------------------------------------------------------------------------
// The gate
// ---------------------------------------------------------------------------

TEST(HeuristicAdmissibilityGuard, PieceDisjointButMoveSharingGroupIsMaxed) {
    // The unsound case: the two patterns track disjoint PIECES, but both are
    // moved by the U face.  One U turn advances both groups at once, so the
    // sum can exceed the true joint distance.  The combinator must MAX.
    std::vector<Pattern> patterns = {
        Pattern{"left", {0, 1}, {2}, true, 100, {0, 1, 2}},  // U moves
        Pattern{"right", {3, 4}, {5}, true, 100, {0, 1, 2}},  // U moves too
    };
    std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1}, true}};
    std::vector<PdbLookup> pdbs = {makeMockPdb("left", 3), makeMockPdb("right", 4)};

    StageHeuristic h(pdbs, groups, patterns, zzAlphabet());

    EXPECT_FALSE(h.groupIsSummed(0));
    EXPECT_EQ(h(CubeState::solved()), 4);  // MAX(3, 4), never 3 + 4 = 7
}

TEST(HeuristicAdmissibilityGuard, DisjointMoveSupportsWithOptInIsSummed) {
    // The sound case: the left block is only moved by L turns and the right
    // block only by R turns, so M(left) and M(right) are disjoint over the
    // {U, R, L} alphabet.  With the explicit opt-in the group is summed.
    std::vector<Pattern> patterns = {
        Pattern{"left", {0, 1}, {2}, true, 100, {12, 13, 14}},  // L, L', L2
        Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},     // R, R', R2
    };
    std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1}, true}};
    std::vector<PdbLookup> pdbs = {makeMockPdb("left", 3), makeMockPdb("right", 4)};

    StageHeuristic h(pdbs, groups, patterns, zzAlphabet());

    EXPECT_TRUE(h.groupIsSummed(0));
    EXPECT_EQ(h(CubeState::solved()), 7);  // 3 + 4
}

TEST(HeuristicAdmissibilityGuard, DisjointMoveSupportsWithoutOptInIsMaxed) {
    // Sum-ability is NEVER inferred from disjointness: the same group as
    // above, but without the explicit opt-in, must be MAX-ed.
    std::vector<Pattern> patterns = {
        Pattern{"left", {0, 1}, {2}, true, 100, {12, 13, 14}},
        Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},
    };
    std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1}}};  // no opt-in
    std::vector<PdbLookup> pdbs = {makeMockPdb("left", 3), makeMockPdb("right", 4)};

    StageHeuristic h(pdbs, groups, patterns, zzAlphabet());

    EXPECT_FALSE(h.groupIsSummed(0));
    EXPECT_EQ(h(CubeState::solved()), 4);  // MAX(3, 4), never 3 + 4 = 7
}

TEST(HeuristicAdmissibilityGuard, OverlappingPiecesAreMaxedEvenWithOptIn) {
    // `verify_disjoint()` remains MANDATORY: even with disjoint move
    // supports and an explicit opt-in, a group whose patterns share a piece
    // must be MAX-ed.
    std::vector<Pattern> patterns = {
        Pattern{"a", {0, 1}, {2}, true, 100, {12, 13, 14}},
        Pattern{"b", {1, 4}, {5}, true, 100, {3, 4, 5}},  // shares corner 1
    };
    std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1}, true}};
    std::vector<PdbLookup> pdbs = {makeMockPdb("a", 3), makeMockPdb("b", 4)};

    EXPECT_FALSE(groups[0].verify_disjoint(patterns));

    StageHeuristic h(pdbs, groups, patterns, zzAlphabet());
    EXPECT_FALSE(h.groupIsSummed(0));
    EXPECT_EQ(h(CubeState::solved()), 4);  // MAX(3, 4), never 3 + 4 = 7
}

TEST(HeuristicAdmissibilityGuard, UndeclaredMoveSupportIsMaxed) {
    // A pattern that declares no move support has an UNKNOWN support, so
    // disjointness cannot be proven and the group must be MAX-ed.  The gate
    // is conservative by construction.
    std::vector<Pattern> patterns = {
        Pattern{"left", {0, 1}, {2}, true, 100},          // no move support
        Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},
    };
    std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1}, true}};
    std::vector<PdbLookup> pdbs = {makeMockPdb("left", 3), makeMockPdb("right", 4)};

    StageHeuristic h(pdbs, groups, patterns, zzAlphabet());
    EXPECT_FALSE(h.groupIsSummed(0));
    EXPECT_EQ(h(CubeState::solved()), 4);
}

TEST(HeuristicAdmissibilityGuard, MoveSupportIsCheckedOverTheStageMoveSetOnly) {
    // Both patterns are moved by the F face -- but F is NOT in this stage's
    // alphabet.  Over {U, R, L} their supports are disjoint, so the gate
    // passes.  Checking over all 18 faces instead would spuriously reject
    // this group and lose the one legitimate sum (finding F-11 item 3).
    std::vector<Pattern> patterns = {
        Pattern{"left", {0, 1}, {2}, true, 100, {6, 12, 13, 14}},
        Pattern{"right", {3, 4}, {5}, true, 100, {6, 3, 4, 5}},
    };
    std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1}, true}};
    std::vector<PdbLookup> pdbs = {makeMockPdb("left", 3), makeMockPdb("right", 4)};

    StageHeuristic h(pdbs, groups, patterns, zzAlphabet());
    EXPECT_TRUE(h.groupIsSummed(0));
    EXPECT_EQ(h(CubeState::solved()), 7);

    // Over the full 18-face alphabet the same two patterns share the F move
    // and the group must be MAX-ed.
    StageHeuristic h_all(pdbs, groups, patterns,
                         rubik::tables::all_18_face_move_indices());
    EXPECT_FALSE(h_all.groupIsSummed(0));
    EXPECT_EQ(h_all(CubeState::solved()), 4);
}

// ---------------------------------------------------------------------------
// MAX remains the default and the universal fallback
// ---------------------------------------------------------------------------

TEST(HeuristicAdmissibilityGuard, NoGroupsIsMaxOfAllPdbs) {
    // The path every solver that declares no groups takes: unchanged.
    std::vector<PdbLookup> pdbs = {
        makeMockPdb("pdb1", 3), makeMockPdb("pdb2", 5), makeMockPdb("pdb3", 2)};
    StageHeuristic h(pdbs, {});
    EXPECT_EQ(h(CubeState::solved()), 5);
}

TEST(HeuristicAdmissibilityGuard, MaxAcrossGroupsIsUnchanged) {
    // One summed group, one MAX-ed group: MAX across groups.
    std::vector<Pattern> patterns = {
        Pattern{"left", {0, 1}, {2}, true, 100, {12, 13, 14}},
        Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},
        Pattern{"coll", {6}, {7}, true, 100, {0, 1, 2}},
    };
    std::vector<DisjointGroup> groups = {
        DisjointGroup{{0, 1}, true},  // summed: disjoint move supports
        DisjointGroup{{2}},           // not opted in -> MAX
    };
    std::vector<PdbLookup> pdbs = {
        makeMockPdb("left", 3), makeMockPdb("right", 4), makeMockPdb("coll", 6)};

    StageHeuristic h(pdbs, groups, patterns, zzAlphabet());
    ASSERT_TRUE(h.groupIsSummed(0));
    ASSERT_FALSE(h.groupIsSummed(1));
    // group 0 = 3 + 4 = 7, group 1 = 6, max = 7
    EXPECT_EQ(h(CubeState::solved()), 7);
}

TEST(HeuristicAdmissibilityGuard, WithoutPatternsEveryGroupIsMaxed) {
    // A combinator built without the stage's patterns and move set cannot
    // evaluate the gate, so it must fall back to MAX.
    std::vector<PdbLookup> pdbs = {makeMockPdb("left", 3), makeMockPdb("right", 4)};
    std::vector<DisjointGroup> groups = {DisjointGroup{{0, 1}, true}};
    StageHeuristic h(pdbs, groups);
    EXPECT_FALSE(h.groupIsSummed(0));
    EXPECT_EQ(h(CubeState::solved()), 4);
}

TEST(HeuristicAdmissibilityGuard, StageConfigReportsWhichGroupsMayBeSummed) {
    rubik::tables::StageConfig config;
    config.stage_name = "test";
    config.patterns = {
        Pattern{"left", {0, 1}, {2}, true, 100, {12, 13, 14}},
        Pattern{"right", {3, 4}, {5}, true, 100, {3, 4, 5}},
        Pattern{"coll", {6}, {7}, true, 100, {0, 1, 2}},
    };
    config.disjoint_groups = {
        DisjointGroup{{0, 1}, true},  // summed
        DisjointGroup{{2}},           // MAX
    };
    config.allowed_moves = zzAlphabet();
    config.goal_predicate = [](const CubeState&) { return true; };
    config.goal_states = {CubeState::solved()};

    EXPECT_TRUE(config.validate());
    EXPECT_TRUE(config.group_may_be_summed(0));
    EXPECT_FALSE(config.group_may_be_summed(1));
    EXPECT_FALSE(config.group_may_be_summed(2));  // out of range
}
