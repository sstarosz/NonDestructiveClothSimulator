#include "clothcad/render/SceneOf.h"

#include <algorithm>
#include <cmath>

namespace clothcad::render {

namespace {

Point pointOf(sim::Vec3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

// The meaning of colours, as on the table.
constexpr Colour bodyColour{0.55f, 0.56f, 0.6f};
constexpr Colour clothColour{0.82f, 0.83f, 0.86f};
constexpr Colour selectedColour{1.0f, 0.75f, 0.27f};
constexpr Colour instanceColour{0.75f, 0.62f, 0.95f};

// The floor: a line every 10 cm for two metres around the middle, stronger every metre.
void addFloor(Scene& scene) {
    Lines fine{{}, {1.0f, 1.0f, 1.0f, 0.06f}}, metres{{}, {1.0f, 1.0f, 1.0f, 0.18f}};
    constexpr int reach = 200;
    for (int at = -reach; at <= reach; at += 10) {
        Lines& into = at % 100 == 0 ? metres : fine;
        const float a = static_cast<float>(at), r = static_cast<float>(reach);
        into.points.insert(into.points.end(), {{a, 0.0f, -r}, {a, 0.0f, r}, {-r, 0.0f, a}, {r, 0.0f, a}});
    }
    scene.lines.push_back(std::move(fine));
    scene.lines.push_back(std::move(metres));
}

} // namespace

std::vector<Point> normalsOf(const std::vector<Point>& points, const std::vector<std::uint32_t>& triangles) {
    std::vector<sim::Vec3> sums(points.size());
    const auto at = [&](std::uint32_t i) { return sim::Vec3{points[i].x, points[i].y, points[i].z}; };
    for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const std::uint32_t a = triangles[t], b = triangles[t + 1], c = triangles[t + 2];
        if (a >= points.size() || b >= points.size() || c >= points.size()) continue;
        const sim::Vec3 face = sim::cross(at(b) - at(a), at(c) - at(a));  // its length is twice the area
        sums[a] = sums[a] + face, sums[b] = sums[b] + face, sums[c] = sums[c] + face;
    }
    std::vector<Point> normals;
    normals.reserve(points.size());
    for (const sim::Vec3& sum : sums) normals.push_back(pointOf(sim::length(sum) > 0.0 ? sim::normalized(sum) : sim::Vec3{0.0, 1.0, 0.0}));
    return normals;
}

Scene sceneOf(const session::Session& session, const OrbitCamera& camera, double aspect, const SceneOptions& options) {
    Scene scene;
    scene.viewProjection = camera.viewProjection(aspect);
    // A light at the camera, a little above it: the side facing the eye is lit.
    scene.toLight = pointOf(sim::normalized(camera.toEye() + sim::Vec3{0.0, 0.4, 0.0}));

    if (options.body) {
        const sim::Surface& body = session.bodySurface();
        Surface surface;
        surface.colour = bodyColour;
        surface.points.reserve(body.points.size());
        for (const sim::Vec3& p : body.points) surface.points.push_back(pointOf(p));
        surface.triangles.reserve(body.triangles.size() * 3);
        for (const auto& t : body.triangles) surface.triangles.insert(surface.triangles.end(), t.begin(), t.end());
        surface.normals = normalsOf(surface.points, surface.triangles);
        scene.surfaces.push_back(std::move(surface));
    }

    const Document& doc = session.document();
    const sim::ClothMesh& mesh = session.shownMesh();
    const sim::Placement& positions = session.shownPositions();
    Lines wire{{}, {0.1f, 0.12f, 0.16f, 0.55f}};
    for (std::size_t i = 0; i < mesh.panels.size() && i < positions.size(); ++i) {
        const sim::PanelMesh& panel = mesh.panels[i];
        if (positions[i].size() != panel.points.size() || panel.triangles.empty()) continue;
        bool instance = false;
        if (doc.contains(panel.panel))
            for (OpId op : doc.operations(panel.panel)) instance = instance || std::holds_alternative<Instance>(doc.get(op));
        const bool selected = options.selected && *options.selected == panel.panel;
        Surface surface;
        surface.colour = selected ? selectedColour : instance ? instanceColour : clothColour;
        surface.panel = panel.panel;
        surface.points.reserve(panel.points.size());
        for (const sim::Vec3& p : positions[i]) surface.points.push_back(pointOf(p));
        for (const auto& t : panel.triangles) {
            surface.triangles.insert(surface.triangles.end(), t.begin(), t.end());
            if (options.mesh)
                for (int side = 0; side < 3; ++side) wire.points.insert(wire.points.end(), {surface.points[t[side]], surface.points[t[(side + 1) % 3]]});
        }
        surface.normals = normalsOf(surface.points, surface.triangles);
        scene.surfaces.push_back(std::move(surface));
    }
    addFloor(scene);
    if (!wire.points.empty()) scene.lines.push_back(std::move(wire));
    return scene;
}

std::optional<std::pair<sim::Vec3, sim::Vec3>> extentOf(const session::Session& session) {
    std::optional<std::pair<sim::Vec3, sim::Vec3>> box;
    const auto take = [&](sim::Vec3 p) {
        if (!box) box = std::pair{p, p};
        box->first = {std::min(box->first.x, p.x), std::min(box->first.y, p.y), std::min(box->first.z, p.z)};
        box->second = {std::max(box->second.x, p.x), std::max(box->second.y, p.y), std::max(box->second.z, p.z)};
    };
    for (const sim::Vec3& p : session.bodySurface().points) take(p);
    for (const std::vector<sim::Vec3>& panel : session.shownPositions())
        for (const sim::Vec3& p : panel) take(p);
    return box;
}

} // namespace clothcad::render
