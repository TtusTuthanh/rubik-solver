#pragma once
#include <cstdint>
#include <queue>
#include <vector>

namespace rubik::core {

// Sentinel value for "unknown / not reached" in byte distance tables.
inline constexpr std::uint8_t kDbSentinel = 255u;

// Byte-table lookup shared by all staged solvers.
// Returns 0 for the sentinel and for out-of-range indices (defensive).
[[nodiscard]] inline int pdbLookup(const std::vector<std::uint8_t>& db,
                                   std::uint32_t idx) {
  if (idx >= db.size()) return 0;
  const std::uint8_t v = db[idx];
  return (v == kDbSentinel) ? 0 : static_cast<int>(v);
}

// Breadth-first fill of a dense distance table.
// `neighbors(node)` must return an iterable of successor node ids.
// Nodes start at kDbSentinel and are filled with their BFS distance (capped
// at 254 so the sentinel stays unique). `start` seeds distance 0.
template <typename NextFn>
[[nodiscard]] inline std::vector<std::uint8_t> bfsDb(std::size_t size,
                                                     std::uint32_t start,
                                                     NextFn&& neighbors) {
  std::vector<std::uint8_t> dist(size, kDbSentinel);
  if (static_cast<std::size_t>(start) >= size) return dist;
  std::queue<std::uint32_t> q;
  dist[start] = 0;
  q.push(start);
  while (!q.empty()) {
    const std::uint32_t cur = q.front();
    q.pop();
    const std::uint8_t d = dist[cur];
    if (d == 254u) continue;  // do not expand past the cap
    for (const std::uint32_t nxt : neighbors(cur)) {
      if (static_cast<std::size_t>(nxt) >= size) continue;
      if (dist[nxt] == kDbSentinel) {
        dist[nxt] = static_cast<std::uint8_t>(d + 1u);
        q.push(nxt);
      }
    }
  }
  return dist;
}

}  // namespace rubik::core
