#pragma once
// What several of the simulation tests measure.

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "twintailor/sim/Body.h"
#include "twintailor/sim/Cloth.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

namespace twintailor::sim::testing {

// Every side of every triangle is used by one triangle (outline) or two (inside).
inline bool watertight(const PanelMesh& mesh) {
    std::multiset<std::pair<uint32_t, uint32_t>> sides;
    for (const auto& t : mesh.triangles)
        for (int i = 0; i < 3; ++i) sides.insert(std::minmax(t[i], t[(i + 1) % 3]));
    for (const auto& side : sides)
        if (sides.count(side) > 2) return false;
    return true;
}

inline double longestSide(const PanelMesh& mesh) {
    double longest = 0.0;
    for (const auto& t : mesh.triangles)
        for (int i = 0; i < 3; ++i) {
            const Vec2 a = mesh.points[t[i]], b = mesh.points[t[(i + 1) % 3]];
            longest = std::max(longest, std::hypot(b.x - a.x, b.y - a.y));
        }
    return longest;
}

// How much the most stretched triangle side has grown, as a part of its flat length.
inline double worstStretch(const ClothMesh& mesh, const Placement& now) {
    double worst = 0.0;
    for (std::size_t p = 0; p < mesh.panels.size(); ++p)
        for (const auto& t : mesh.panels[p].triangles)
            for (int i = 0; i < 3; ++i) {
                const Vec2 a = mesh.panels[p].points[t[i]], b = mesh.panels[p].points[t[(i + 1) % 3]];
                const double flat = std::hypot(b.x - a.x, b.y - a.y);
                worst = std::max(worst, length(now[p][t[(i + 1) % 3]] - now[p][t[i]]) / flat - 1.0);
            }
    return worst;
}

inline void run(Cloth& cloth, const Body& body, double seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 60.0); ++i) cloth.step(body);
}

} // namespace twintailor::sim::testing
