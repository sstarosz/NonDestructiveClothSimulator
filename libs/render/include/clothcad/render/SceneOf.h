#pragma once
// What the 3D view shows of a session: the body, the cloth (draped, or the
// panels where they are arranged), the floor, and the mesh's triangles when
// asked. Colours keep their meaning: the selected panel amber, an instance
// violet.
//
//   render::Scene scene = render::sceneOf(session, camera, width / double(height), {.selected = panel});

#include <optional>
#include <utility>

#include "clothcad/render/Camera.h"
#include "clothcad/render/Scene.h"
#include "clothcad/session/Session.h"

namespace clothcad::render {

struct SceneOptions {
    std::optional<PanelId> selected{};
    bool body = true;   // the body shown
    bool mesh = false;  // the cloth's triangles drawn over it
};

Scene sceneOf(const session::Session& session, const OrbitCamera& camera, double aspect, const SceneOptions& options);

// The box around the body and the cloth, to frame; empty when there is nothing.
std::optional<std::pair<sim::Vec3, sim::Vec3>> extentOf(const session::Session& session);

// Normals at the points of a surface: the faces around each, weighted by their area.
std::vector<Point> normalsOf(const std::vector<Point>& points, const std::vector<std::uint32_t>& triangles);

} // namespace clothcad::render
