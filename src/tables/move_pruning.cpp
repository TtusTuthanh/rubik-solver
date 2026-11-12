// MovePruningTable is header-only (see src/tables/move_pruning.hpp): the
// solver libraries link `rubik_core` only and must be able to build a
// pruning table without gaining a link dependency on `rubik_tables`.
//
// This translation unit remains so that `rubik_tables` keeps compiling the
// header, and so the header's inline definitions are instantiated in exactly
// one place.
#include "tables/move_pruning.hpp"
