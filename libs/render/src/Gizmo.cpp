#include "clothcad/render/Gizmo.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "clothcad/render/SceneOf.h"
#include "clothcad/sim/Layout.h"

namespace clothcad::render {

namespace {

constexpr double arrowPixels = 90.0;  // how long an arrow is drawn, in screen pixels
constexpr double reachPixels = 8.0;   // how near the mouse must come to a handle to take hold of it
constexpr int roundSides = 12;        // sides of an arrow's shaft and head

const sim::Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
// The axes' colours: x red, y green, z blue, as Maya has them.
constexpr Colour axisColours[3] = {{0.90f, 0.39f, 0.39f}, {0.47f, 0.82f, 0.47f}, {0.43f, 0.59f, 0.94f}};
constexpr Colour selectedColour{1.0f, 0.75f, 0.27f};
constexpr Colour middleColour{0.92f, 0.92f, 0.92f};

Point pointOf(sim::Vec3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

std::optional<int> axisOf(GizmoHandle handle) {
    switch (handle) {
    case GizmoHandle::AlongX: return 0;
    case GizmoHandle::AlongY: return 1;
    case GizmoHandle::AlongZ: return 2;
    case GizmoHandle::AcrossTheView: break;
    }
    return std::nullopt;
}

// How many cm one screen pixel is at the gizmo's centre.
double cmPerPixel(const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size) {
    const double depth = std::max(sim::dot(gizmo.centre - camera.eye(), camera.toEye() * -1.0), 1.0);
    const double perPicturePixel = 2.0 * depth * std::tan(camera.fieldOfView * std::numbers::pi / 180.0 / 2.0) / std::max(size.height, 1);
    return perPicturePixel * gizmo.pixel;
}

// Where the gizmo is drawn: its centre and the tips of the arrows long enough to show.
struct Drawn {
    PicturePoint centre;
    std::optional<PicturePoint> tips[3];
    double arrow = 0.0;  // cm
};
std::optional<Drawn> drawnAt(const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size) {
    const std::optional<PicturePoint> centre = pictureOf(camera, size, gizmo.centre);
    if (!centre) return std::nullopt;
    Drawn drawn{*centre, {}, arrowPixels * cmPerPixel(gizmo, camera, size)};
    for (int i = 0; i < 3; ++i)
        if (const std::optional<PicturePoint> tip = pictureOf(camera, size, gizmo.centre + axes[i] * drawn.arrow))
            if (std::hypot(tip->x - centre->x, tip->y - centre->y) > 2.0 * reachPixels * gizmo.pixel) drawn.tips[i] = tip;
    return drawn;
}

double distanceToSegment(PicturePoint p, PicturePoint a, PicturePoint b) {
    const double dx = b.x - a.x, dy = b.y - a.y, lengthSquared = dx * dx + dy * dy;
    const double t = lengthSquared > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0) : 0.0;
    return std::hypot(a.x + t * dx - p.x, a.y + t * dy - p.y);
}

// Where along the line `from + axis * t` the ray passes closest; empty when they run (almost) together.
std::optional<double> along(sim::Vec3 from, sim::Vec3 axis, const Ray& ray) {
    const double b = sim::dot(axis, ray.direction), across = 1.0 - b * b;
    if (across < 1e-3) return std::nullopt;
    const sim::Vec3 between = from - ray.from;
    return std::clamp((b * sim::dot(ray.direction, between) - sim::dot(axis, between)) / across, -5000.0, 5000.0);
}

// A round solid along `axis` from `start`: radius `from` at the start, `to` at the end, `length` long, closed at both ends.
void addRound(Surface& into, sim::Vec3 start, sim::Vec3 axis, double length, double from, double to) {
    const sim::Vec3 u = sim::normalized(sim::cross(axis, std::abs(axis.y) < 0.9 ? sim::Vec3{0, 1, 0} : sim::Vec3{1, 0, 0}));
    const sim::Vec3 v = sim::cross(axis, u);
    const auto first = static_cast<std::uint32_t>(into.points.size());
    const sim::Vec3 end = start + axis * length;
    for (int side = 0; side < roundSides; ++side) {
        const double angle = 2.0 * std::numbers::pi * side / roundSides;
        const sim::Vec3 out = u * std::cos(angle) + v * std::sin(angle);
        into.points.push_back(pointOf(start + out * from));
        into.points.push_back(pointOf(end + out * to));
    }
    const auto startMiddle = static_cast<std::uint32_t>(into.points.size());
    into.points.push_back(pointOf(start));
    into.points.push_back(pointOf(end));
    for (std::uint32_t side = 0; side < roundSides; ++side) {
        const std::uint32_t next = (side + 1) % roundSides;
        const std::uint32_t a0 = first + 2 * side, a1 = a0 + 1, b0 = first + 2 * next, b1 = b0 + 1;
        into.triangles.insert(into.triangles.end(), {a0, b0, b1, a0, b1, a1, startMiddle, b0, a0, startMiddle + 1, a1, b1});
    }
}

void addCube(Surface& into, sim::Vec3 centre, double half) {
    const auto first = static_cast<std::uint32_t>(into.points.size());
    for (int corner = 0; corner < 8; ++corner)
        into.points.push_back(pointOf(centre + sim::Vec3{corner & 1 ? half : -half, corner & 2 ? half : -half, corner & 4 ? half : -half}));
    // Two triangles for each face, the corners numbered by their bits x, y, z.
    constexpr std::uint32_t faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (const auto& f : faces) into.triangles.insert(into.triangles.end(), {first + f[0], first + f[1], first + f[2], first + f[0], first + f[2], first + f[3]});
}

} // namespace

std::optional<sim::Vec3> gizmoCentre(const session::Session& session, PanelId panel) {
    if (!session.mesh() || !session.document().contains(panel)) return std::nullopt;
    const sim::PanelMesh* mesh = session.mesh()->find(panel);
    if (!mesh) return std::nullopt;
    const Arrangement place = sim::arrangementOf(session.document(), *mesh);
    return sim::Vec3{place.x, place.y, place.z};
}

std::optional<GizmoHandle> handleAt(const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, PicturePoint at) {
    const std::optional<Drawn> drawn = drawnAt(gizmo, camera, size);
    if (!drawn) return std::nullopt;
    const double reach = reachPixels * gizmo.pixel;
    if (std::abs(at.x - drawn->centre.x) <= reach && std::abs(at.y - drawn->centre.y) <= reach) return GizmoHandle::AcrossTheView;
    std::optional<GizmoHandle> nearest;
    double best = reach;
    constexpr GizmoHandle arrows[3] = {GizmoHandle::AlongX, GizmoHandle::AlongY, GizmoHandle::AlongZ};
    for (int i = 0; i < 3; ++i)
        if (drawn->tips[i])
            if (const double d = distanceToSegment(at, drawn->centre, *drawn->tips[i]); d <= best) {
                best = d;
                nearest = arrows[i];
            }
    return nearest;
}

std::optional<sim::Vec3> moveBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                                PicturePoint now, double step) {
    const auto snapped = [step](double value) { return step > 0.0 ? std::round(value / step) * step : value; };
    const Ray from = rayThrough(camera, size, pressed), to = rayThrough(camera, size, now);
    if (const std::optional<int> axis = axisOf(handle)) {
        const std::optional<double> start = along(gizmo.centre, axes[*axis], from), end = along(gizmo.centre, axes[*axis], to);
        if (!start || !end) return std::nullopt;
        return axes[*axis] * snapped(*end - *start);
    }
    // Across the view: in the plane through the centre that faces the camera.
    const std::optional<sim::Vec3> by = acrossTheView(camera, size, gizmo.centre, pressed, now);
    if (!by) return std::nullopt;
    return sim::Vec3{snapped(by->x), snapped(by->y), snapped(by->z)};
}

void addGizmo(Scene& scene, const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, std::optional<GizmoHandle> lit) {
    const std::optional<Drawn> drawn = drawnAt(gizmo, camera, size);
    if (!drawn) return;
    const double arrow = drawn->arrow;
    constexpr GizmoHandle arrows[3] = {GizmoHandle::AlongX, GizmoHandle::AlongY, GizmoHandle::AlongZ};
    for (int i = 0; i < 3; ++i) {
        if (!drawn->tips[i]) continue;
        Surface surface;
        surface.colour = lit == arrows[i] ? selectedColour : axisColours[i];
        const double shaft = arrow * 0.76;
        addRound(surface, gizmo.centre + axes[i] * (arrow * 0.08), axes[i], shaft - arrow * 0.08, arrow * 0.022, arrow * 0.022);
        addRound(surface, gizmo.centre + axes[i] * shaft, axes[i], arrow - shaft, arrow * 0.075, 0.0);
        surface.normals = normalsOf(surface.points, surface.triangles);
        scene.overlay.push_back(std::move(surface));
    }
    Surface middle;
    middle.colour = lit == GizmoHandle::AcrossTheView ? selectedColour : middleColour;
    addCube(middle, gizmo.centre, arrow * 0.07);
    middle.normals = normalsOf(middle.points, middle.triangles);
    scene.overlay.push_back(std::move(middle));
}

void addCornerAxes(Scene& scene, const OrbitCamera& camera, ViewSize size, double pixel) {
    // A spot a little in from the bottom left corner, as deep as the target,
    // and arrows as long there as `cornerPixels` on the screen.
    constexpr double cornerPixels = 26.0, inset = 40.0;
    const Ray ray = rayThrough(camera, size, {inset * pixel, size.height - inset * pixel});
    const double depth = camera.distance / std::max(sim::dot(ray.direction, camera.toEye() * -1.0), 1e-3);
    const sim::Vec3 at = ray.from + ray.direction * depth;
    const double perPixel = 2.0 * camera.distance * std::tan(camera.fieldOfView * std::numbers::pi / 180.0 / 2.0) / std::max(size.height, 1);
    const double arrow = cornerPixels * pixel * perPixel;
    for (int i = 0; i < 3; ++i) {
        Surface surface;
        surface.colour = axisColours[i];
        addRound(surface, at, axes[i], arrow * 0.72, arrow * 0.04, arrow * 0.04);
        addRound(surface, at + axes[i] * (arrow * 0.72), axes[i], arrow * 0.28, arrow * 0.11, 0.0);
        surface.normals = normalsOf(surface.points, surface.triangles);
        scene.overlay.push_back(std::move(surface));
    }
}

} // namespace clothcad::render
