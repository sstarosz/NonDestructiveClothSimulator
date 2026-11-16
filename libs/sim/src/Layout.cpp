#include "clothcad/sim/Layout.h"

#include <algorithm>
#include <cmath>

#include "clothcad/core/Drawing.h"

namespace clothcad::sim {

namespace {

struct Box {
    Vec2 low, high;
    Vec2 centre() const { return {(low.x + high.x) / 2.0, (low.y + high.y) / 2.0}; }
};

Box boxOf(const PanelMesh& mesh) {
    if (mesh.points.empty()) return {};
    Box box{mesh.points.front(), mesh.points.front()};
    for (const Vec2& p : mesh.points) {
        box.low = {std::min(box.low.x, p.x), std::min(box.low.y, p.y)};
        box.high = {std::max(box.high.x, p.x), std::max(box.high.y, p.y)};
    }
    return box;
}

double radians(double degrees) { return degrees * (pi / 180.0); }

} // namespace

Arrangement arrangementOf(const Document& doc, const PanelMesh& mesh) {
    if (!doc.contains(mesh.panel)) return {};
    const Panel& panel = doc.get(mesh.panel);
    if (panel.arrangement.placed) return panel.arrangement;
    // Where the table has the panel's centre, a step in front of the body.
    const Vec2 centre = turned(boxOf(mesh).centre(), panel.angle);
    Arrangement standing;
    standing.x = panel.x + centre.x;
    standing.y = panel.y + centre.y;
    standing.z = unarrangedDistance;
    standing.roll = panel.angle;
    return standing;
}

Placement arranged(const Document& doc, const ClothMesh& cloth) {
    Placement placement;
    placement.reserve(cloth.panels.size());
    for (const PanelMesh& mesh : cloth.panels) {
        const Arrangement in = arrangementOf(doc, mesh);
        const Box box = boxOf(mesh);
        const Vec2 centre = box.centre();
        const double width = box.high.x - box.low.x;
        const double wrap = radians(in.wrap);
        const double bendRadius = std::abs(wrap) > 1e-6 && width > 1e-9 ? width / wrap : 0.0;
        const double cr = std::cos(radians(in.roll)), sr = std::sin(radians(in.roll));
        const double ct = std::cos(radians(in.tilt)), st = std::sin(radians(in.tilt));
        const double cy = std::cos(radians(in.turn)), sy = std::sin(radians(in.turn));

        std::vector<Vec3> positions;
        positions.reserve(mesh.points.size());
        for (const Vec2& point : mesh.points) {
            // Flat, about the panel's centre; then bent around an upright axis behind it.
            Vec3 p{point.x - centre.x, point.y - centre.y, 0.0};
            if (bendRadius != 0.0) {
                const double angle = p.x / bendRadius;
                p = {bendRadius * std::sin(angle), p.y, bendRadius * (std::cos(angle) - 1.0)};
            }
            p = {p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z};  // roll, in its own plane
            p = {p.x, p.y * ct - p.z * st, p.y * st + p.z * ct};  // tilt, about its horizontal axis
            p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy}; // turn, about the vertical
            positions.push_back({p.x + in.x, p.y + in.y, p.z + in.z});
        }
        placement.push_back(std::move(positions));
    }
    return placement;
}

} // namespace clothcad::sim
