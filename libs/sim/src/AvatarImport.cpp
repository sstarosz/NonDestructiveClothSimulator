#include "clothcad/sim/AvatarImport.h"

#include <algorithm>
#include <cmath>

namespace clothcad::sim {

namespace {

constexpr double shortestPerson = 120.0, tallestPerson = 230.0;  // cm
constexpr double usualPerson = 170.0;
constexpr double barelyLonger = 0.9;  // a side this much of the longest counts as long as it

Vec3 directionOf(Axis axis) {
    switch (axis) {
    case Axis::X: return {1, 0, 0};
    case Axis::Y: return {0, 1, 0};
    case Axis::Z: return {0, 0, 1};
    case Axis::MinusX: return {-1, 0, 0};
    case Axis::MinusY: return {0, -1, 0};
    case Axis::MinusZ: return {0, 0, -1};
    }
    return {0, 1, 0};
}

// The file's directions that become the document's x, y and z.
struct Frame {
    Vec3 right, up, facing;
    explicit Frame(const AvatarImport& how) : up(directionOf(how.up)), facing(directionOf(how.facing)) { right = cross(up, facing); }
    Vec3 turn(Vec3 file) const { return {dot(file, right), dot(file, up), dot(file, facing)}; }
    Vec3 turnBack(Vec3 here) const { return right * here.x + up * here.y + facing * here.z; }
};

struct Box {
    Vec3 low, high;
};

Box boxOf(const std::vector<Vec3>& points) {
    if (points.empty()) return {};
    Box box{points.front(), points.front()};
    for (const Vec3& p : points) {
        box.low = {std::min(box.low.x, p.x), std::min(box.low.y, p.y), std::min(box.low.z, p.z)};
        box.high = {std::max(box.high.x, p.x), std::max(box.high.y, p.y), std::max(box.high.z, p.z)};
    }
    return box;
}

} // namespace

Vec3 sizeOf(const Surface& file) {
    const Box box = boxOf(file.points);
    return box.high - box.low;
}

double along(Vec3 size, Axis axis) { return std::abs(dot(size, directionOf(axis))); }

Axis usualFacing(Axis up) {
    switch (up) {
    case Axis::Z: return Axis::MinusY;
    case Axis::MinusZ: return Axis::Y;
    default: return Axis::Z;
    }
}

AvatarImport guessImport(const Surface& file) {
    const Vec3 size = sizeOf(file);
    const double longest = std::max({size.x, size.y, size.z});
    AvatarImport how;
    how.up = size.y >= barelyLonger * longest ? Axis::Y : size.z >= barelyLonger * longest ? Axis::Z : Axis::X;
    how.facing = usualFacing(how.up);
    const double tall = along(size, how.up);
    if (tall > 0.0) {
        // How many times too tall or too short a person it is in a unit.
        const auto off = [&](const FileUnit& unit) { return std::abs(std::log(tall * unit.cm / usualPerson)); };
        how.unit = std::ranges::min_element(fileUnits, {}, off)->cm;
        // No two units are near enough to both make a person of it: the nearest is the one that does, if any.
        const double nearest = tall * how.unit;
        if (nearest < shortestPerson || nearest > tallestPerson) how.rescale = usualPerson / nearest;
    }
    return standing(file, how);
}

AvatarImport standing(const Surface& file, AvatarImport how) {
    // The file's box, turned: its corners stay corners, as the turn only swaps and flips axes.
    const Box own = boxOf(file.points);
    const Frame frame(how);
    const Box box = boxOf({frame.turn(own.low) * how.cmPerUnit(), frame.turn(own.high) * how.cmPerUnit()});
    how.moved = {-(box.low.x + box.high.x) / 2.0, -box.low.y, -(box.low.z + box.high.z) / 2.0};
    return how;
}

Surface imported(const Surface& file, const AvatarImport& how) {
    const Frame frame(how);
    const Vec3 moved{how.moved.x, how.moved.y, how.moved.z};
    Surface surface{.points = {}, .triangles = file.triangles};
    surface.points.reserve(file.points.size());
    const double scale = how.cmPerUnit();
    for (const Vec3& p : file.points) surface.points.push_back(frame.turn(p) * scale + moved);
    return surface;
}

Vec3 inFile(const AvatarImport& how, Vec3 point) {
    const Vec3 moved{how.moved.x, how.moved.y, how.moved.z};
    return Frame(how).turnBack((point - moved) * (1.0 / how.cmPerUnit()));
}

Vec3 directionInFile(const AvatarImport& how, Vec3 direction) { return Frame(how).turnBack(direction); }

} // namespace clothcad::sim
