#pragma once
#include <cstdlib>

namespace sample {

struct ZoneCoord { int x = 0, y = 0; };
struct Position  { int x = 50, y = 50; };

inline int manhattan_distance(ZoneCoord a, ZoneCoord b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

inline bool is_adjacent(ZoneCoord a, ZoneCoord b) {
    return manhattan_distance(a, b) <= 1 && !(a.x == b.x && a.y == b.y);
}

// boundary threshold: within 20 units of zone edge (zone is 100x100)
inline bool is_near_boundary(Position pos) {
    return pos.x <= 20 || pos.x >= 80 || pos.y <= 20 || pos.y >= 80;
}

} // namespace sample
