#pragma once
// What the 3D view shows of a session: the body, the cloth (draped, or the
// panels where they are arranged), the floor, and the mesh's triangles when
// asked. Colours keep their meaning: the selected panel amber, and, when
// instances are marked, an instance violet. A panel's front (the side the table shows) has that colour; its back
// is a darker, cooler shade of it. A seam that is still open is lined in blue
// along both its edges, with a thin line from each particle to the one it is
// sewn to, where the cloth has them now. A closed seam has no lines, and both
// its sides are shaded as one cloth, with the same normals; the selected seam
// is lined in amber, wider, open or closed. One the cloth could not close
// (sim::Cloth::openSeams) is lined in red. A hidden seam has no lines. A
// hidden panel is left out, with its side of every seam; a deactivated one is
// seen through. The body can be see-through too, to show cloth inside it. A
// body being chosen (Session::previewBody) is drawn in the avatar's place.
//
//   render::Scene scene = render::sceneOf(session, camera, width / double(height), {.selected = panel});
//
// The body is the same from picture to picture, and may have millions of
// triangles: a view that asks again and again keeps a BodyShape, and the body
// is made into a Shape once.
//
//   render::BodyShape body;                                     // beside the view
//   scene = render::sceneOf(session, camera, aspect, {}, &body);
//   box = render::extentOf(session, &body);

#include <memory>
#include <optional>
#include <utility>

#include "twintailor/render/Camera.h"
#include "twintailor/render/Scene.h"
#include "twintailor/session/Session.h"

namespace twintailor::render {

struct SceneOptions {
    std::optional<PanelId> selected{};
    bool body = true;   // the body shown
    bool mesh = false;  // the cloth's triangles drawn over it
    std::optional<SeamId> seam{};  // the selected seam
    double pixel = 1.0;            // picture pixels in one pixel of the screen's own scale: how wide lines are
    bool seeThroughBody = false;   // the body drawn so that cloth inside it shows
    bool markInstances = false;    // instances drawn violet, as the table has them
    friend bool operator==(const SceneOptions&, const SceneOptions&) = default;
};

// A box in space: its lowest and its highest corner.
using Box = std::pair<sim::Vec3, sim::Vec3>;

// The body as the latest scene showed it, with what it was made from.
struct BodyShape {
    std::shared_ptr<const sim::Surface> of;  // held, so that no other body is ever taken for it
    std::shared_ptr<const Shape> shape;
    std::optional<Box> box;  // around it
};

// With `body`, what it holds is used if it is of the body shown now, and is
// made again, into it, if not.
Scene sceneOf(const session::Session& session, const OrbitCamera& camera, double aspect, const SceneOptions& options, BodyShape* body = nullptr);

// The box around the body and the panels shown, to frame; empty when there is nothing.
std::optional<Box> extentOf(const session::Session& session, const BodyShape* body = nullptr);

// Normals at the points of a surface: the faces around each, weighted by their area.
std::vector<Point> normalsOf(const std::vector<Point>& points, const std::vector<std::uint32_t>& triangles);

} // namespace twintailor::render
