#include "clothcad/render/SceneOf.h"

#include <algorithm>
#include <cmath>

#include "clothcad/sim/Normals.h"

namespace clothcad::render {

namespace {

Point pointOf(sim::Vec3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

// The meaning of colours, as on the table.
constexpr Colour bodyColour{0.55f, 0.56f, 0.6f};
constexpr Colour clothColour{0.82f, 0.83f, 0.86f};
constexpr Colour selectedColour{1.0f, 0.75f, 0.27f};
constexpr Colour instanceColour{0.75f, 0.62f, 0.95f};
// The table's seam blue, deeper: it is seen against pale cloth, not the dark table.
constexpr Colour seamColour{0.18f, 0.46f, 0.98f};

constexpr float seamWidth = 2.0f, selectedSeamWidth = 4.0f;      // a seam's edges, in pixels of the screen's own scale
constexpr float threadWidth = 1.0f;                              // the lines from each particle to the one it is sewn to: thin, so a cross reads
constexpr float threadAlpha = 0.75f;                             // many lie close together: a little of what is behind shows
constexpr float seamLift = 0.5f;                             // cm
constexpr float inactiveAlpha = 0.3f;  // a deactivated panel is seen through
constexpr float seeThroughBodyAlpha = 0.35f;

Point normalOf(sim::Vec3 sum) { return pointOf(sim::length(sum) > 0.0 ? sim::normalized(sum) : sim::Vec3{0.0, 1.0, 0.0}); }

// A panel's back: its colour, darker and cooler, so that which way it faces
// shows at a glance and what it is (selected, an instance) still does.
Colour behind(Colour front) { return {front.red * 0.58f, front.green * 0.61f, front.blue * 0.77f, front.alpha}; }

// The lines of every seam there is something to see of: one along both its sewn
// edges, and one from each particle to the particle it is sewn to, which shows
// which end meets which (a twisted seam's lines cross). A closed seam has none,
// so that the cloth is seen as it will be, unless it is the selected one, which
// is in amber and comes last, to lie on top. A seam switched off has none
// either, and a hidden panel's side of a seam is left out with the lines to it.
void addSeams(Scene& scene, const session::Session& session, const SceneOptions& options) {
    const Document& doc = session.document();
    const sim::ClothMesh& mesh = session.shownMesh();
    const sim::Placement& positions = session.shownPositions();
    const float pixel = static_cast<float>(options.pixel);
    const auto placed = [&](std::size_t panel, std::uint32_t point) {
        return panel < positions.size() && panel < mesh.panels.size() && positions[panel].size() == mesh.panels[panel].points.size() && point < positions[panel].size();
    };
    std::vector<Lines> selected;
    // A seam's stitches follow each other.
    for (std::size_t from = 0, to = 0; from < mesh.stitches.size(); from = to) {
        const SeamId id = mesh.stitches[from].seam;
        for (to = from + 1; to < mesh.stitches.size() && mesh.stitches[to].seam == id; ++to) {}
        // A mesh a few frames old may name a seam that is gone.
        if (!doc.contains(id) || !session.seamShown(id)) continue;
        const bool chosen = options.seam == id;
        Colour colour = chosen ? selectedColour : seamColour;
        Lines threads{.points = {}, .colour = colour, .width = threadWidth * pixel, .lift = seamLift};
        if (!chosen) threads.colour.alpha = threadAlpha;
        bool closed = true;
        for (std::size_t k = from; k < to; ++k) {
            const sim::Stitch& stitch = mesh.stitches[k];
            if (!placed(stitch.panelA, stitch.pointA) || !placed(stitch.panelB, stitch.pointB)) continue;
            const sim::Vec3 a = positions[stitch.panelA][stitch.pointA], b = positions[stitch.panelB][stitch.pointB];
            closed = closed && sim::length(a - b) <= sim::joinedWithin;
            if (session.panelShown(mesh.panels[stitch.panelA].panel) && session.panelShown(mesh.panels[stitch.panelB].panel))
                threads.points.insert(threads.points.end(), {pointOf(a), pointOf(b)});
        }
        if (closed && !chosen) continue;
        Lines edges{.points = {}, .colour = colour, .width = (chosen ? selectedSeamWidth : seamWidth) * pixel, .lift = seamLift};
        const Seam& seam = doc.get(id);
        for (const SeamSide& side : {seam.a, seam.b}) {
            const sim::PanelMesh* panel = mesh.find(side.panel);
            if (!panel || !session.panelShown(side.panel)) continue;
            const auto i = static_cast<std::size_t>(panel - mesh.panels.data());
            const sim::EdgeParticles* along = panel->along(side.edge);
            if (!along || i >= positions.size() || positions[i].size() != panel->points.size()) continue;
            for (std::size_t k = 1; k < along->points.size(); ++k)
                edges.points.insert(edges.points.end(), {pointOf(positions[i][along->points[k - 1]]), pointOf(positions[i][along->points[k]])});
        }
        for (Lines* lines : {&edges, &threads}) {
            if (lines->points.empty()) continue;
            if (chosen) selected.push_back(std::move(*lines));
            else scene.lines.push_back(std::move(*lines));
        }
    }
    for (Lines& lines : selected) scene.lines.push_back(std::move(lines));
}

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
    for (const sim::Vec3& sum : sums) normals.push_back(normalOf(sum));
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
        if (options.seeThroughBody) {
            surface.colour.alpha = seeThroughBodyAlpha;
            surface.nearestOnly = true;
        }
        scene.surfaces.push_back(std::move(surface));
    }

    const Document& doc = session.document();
    const sim::ClothMesh& mesh = session.shownMesh();
    const sim::Placement& positions = session.shownPositions();
    Lines wire{{}, {0.1f, 0.12f, 0.16f, 0.55f}};
    const std::vector<std::vector<sim::Vec3>> normals = sim::clothNormals(mesh, positions);
    for (std::size_t i = 0; i < mesh.panels.size() && i < positions.size(); ++i) {
        const sim::PanelMesh& panel = mesh.panels[i];
        if (positions[i].size() != panel.points.size() || panel.triangles.empty() || !session.panelShown(panel.panel)) continue;
        bool instance = false;
        if (doc.contains(panel.panel))
            for (OpId op : doc.operations(panel.panel)) instance = instance || std::holds_alternative<Instance>(doc.get(op));
        const bool selected = options.selected && *options.selected == panel.panel;
        Surface surface;
        surface.colour = selected ? selectedColour : instance ? instanceColour : clothColour;
        if (!session.panelActive(panel.panel)) surface.colour.alpha = inactiveAlpha;
        surface.back = behind(surface.colour);
        surface.panel = panel.panel;
        surface.points.reserve(panel.points.size());
        for (const sim::Vec3& p : positions[i]) surface.points.push_back(pointOf(p));
        for (const auto& t : panel.triangles) {
            surface.triangles.insert(surface.triangles.end(), t.begin(), t.end());
            if (options.mesh)
                for (int side = 0; side < 3; ++side) wire.points.insert(wire.points.end(), {surface.points[t[side]], surface.points[t[(side + 1) % 3]]});
        }
        surface.normals.reserve(panel.points.size());
        for (const sim::Vec3& n : normals[i]) surface.normals.push_back(pointOf(n));
        scene.surfaces.push_back(std::move(surface));
    }
    addFloor(scene);
    if (!wire.points.empty()) scene.lines.push_back(std::move(wire));
    addSeams(scene, session, options);
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
    const std::vector<sim::PanelMesh>& panels = session.shownMesh().panels;
    const sim::Placement& positions = session.shownPositions();
    for (std::size_t i = 0; i < positions.size() && i < panels.size(); ++i)
        if (session.panelShown(panels[i].panel))
            for (const sim::Vec3& p : positions[i]) take(p);
    return box;
}

} // namespace clothcad::render
