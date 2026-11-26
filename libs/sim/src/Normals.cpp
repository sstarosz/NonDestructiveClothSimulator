#include "clothcad/sim/Normals.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>

namespace clothcad::sim {

std::vector<std::vector<Vec3>> clothNormals(const ClothMesh& mesh, const Placement& positions) {
    // For every panel's particles, the faces around each added up.
    std::vector<std::vector<Vec3>> sums(mesh.panels.size());
    std::vector<std::uint32_t> first(mesh.panels.size() + 1, 0);  // where each panel's particles start among all
    for (std::size_t i = 0; i < mesh.panels.size(); ++i) {
        const PanelMesh& panel = mesh.panels[i];
        const bool placed = i < positions.size() && positions[i].size() == panel.points.size();
        first[i + 1] = first[i] + (placed ? static_cast<std::uint32_t>(panel.points.size()) : 0);
        if (!placed) continue;
        sums[i].resize(panel.points.size());
        for (const auto& t : panel.triangles) {
            const Vec3 face = cross(positions[i][t[1]] - positions[i][t[0]], positions[i][t[2]] - positions[i][t[0]]);  // its length is twice the area
            for (const std::uint32_t corner : t) sums[i][corner] = sums[i][corner] + face;
        }
    }

    // The particles closed stitches bring together, gathered into groups: each
    // points towards its group's first, and says whether it is turned round
    // against the one it points to.
    std::vector<std::uint32_t> towards(first.back());
    std::iota(towards.begin(), towards.end(), 0u);
    std::vector<char> turned(first.back(), 0);
    const auto firstOfGroup = [&](std::uint32_t particle) {
        bool flipped = false;
        while (towards[particle] != particle) {
            flipped = flipped != (turned[particle] != 0);
            particle = towards[particle];
        }
        return std::pair{particle, flipped};
    };
    struct Member {
        std::uint32_t panel, point;
    };
    std::vector<Member> members;
    for (const Stitch& stitch : mesh.stitches) {
        const std::size_t a = stitch.panelA, b = stitch.panelB;
        if (a >= sums.size() || b >= sums.size() || stitch.pointA >= sums[a].size() || stitch.pointB >= sums[b].size()) continue;
        if (length(positions[a][stitch.pointA] - positions[b][stitch.pointB]) > joinedWithin) continue;
        const auto [groupA, flippedA] = firstOfGroup(first[a] + stitch.pointA);
        const auto [groupB, flippedB] = firstOfGroup(first[b] + stitch.pointB);
        if (groupA != groupB) {
            towards[groupB] = groupA;
            turned[groupB] = (flippedA != flippedB) != stitch.reversed;
        }
        members.push_back({stitch.panelA, stitch.pointA});
        members.push_back({stitch.panelB, stitch.pointB});
    }
    // Each particle once, however many stitches hold it.
    const auto order = [](const Member& m) { return std::pair{m.panel, m.point}; };
    std::ranges::sort(members, {}, order);
    const auto repeated = std::ranges::unique(members, {}, order);
    members.erase(repeated.begin(), repeated.end());

    std::vector<Vec3> shared(first.back());
    for (const Member& m : members) {
        const auto [group, flipped] = firstOfGroup(first[m.panel] + m.point);
        shared[group] = shared[group] + sums[m.panel][m.point] * (flipped ? -1.0 : 1.0);
    }
    for (const Member& m : members) {
        const auto [group, flipped] = firstOfGroup(first[m.panel] + m.point);
        const Vec3 together = shared[group] * (flipped ? -1.0 : 1.0);
        if (dot(together, sums[m.panel][m.point]) > 0.0) sums[m.panel][m.point] = together;
    }

    for (std::vector<Vec3>& panel : sums)
        for (Vec3& sum : panel) sum = length(sum) > 0.0 ? normalized(sum) : Vec3{0.0, 1.0, 0.0};
    return sums;
}

} // namespace clothcad::sim
