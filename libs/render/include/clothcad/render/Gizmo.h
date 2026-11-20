#pragma once
// The move gizmo on the selected panel in the 3D view: an arrow along each
// axis (x red, y green, z blue) moves the panel along that axis only, and the
// square in the middle moves it across the view. It stands at the point the
// panel's arrangement places (its x, y and z in space), is drawn in front of
// everything, and is the same size on the screen at any distance.
//
//   const render::Gizmo gizmo{.centre = *render::gizmoCentre(session, panel), .pixel = devicePixelRatio};
//   std::optional<render::GizmoHandle> held = render::handleAt(gizmo, camera, size, pressed);
//   std::optional<sim::Vec3> by = render::moveBy(gizmo, *held, camera, size, pressed, now, 0.1);
//   render::addGizmo(scene, gizmo, camera, size, held);  // into scene.overlay

#include <optional>

#include "clothcad/render/Camera.h"
#include "clothcad/render/Picking.h"
#include "clothcad/render/Scene.h"
#include "clothcad/session/Session.h"

namespace clothcad::render {

enum class GizmoHandle { AcrossTheView, AlongX, AlongY, AlongZ };

struct Gizmo {
    sim::Vec3 centre;
    // Picture pixels per pixel of the screen's own scale, so that the gizmo
    // is as big, and as easy to take hold of, on a 4K display.
    double pixel = 1.0;
};

// Where the gizmo of a panel stands: where its arrangement places it. Empty
// when the panel has no cloth mesh to show.
std::optional<sim::Vec3> gizmoCentre(const session::Session& session, PanelId panel);

// The handle drawn at a place in the picture, if any: the middle square
// first, then the nearest arrow. An arrow pointing almost at the eye is too
// short to be shown or taken hold of.
std::optional<GizmoHandle> handleAt(const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, PicturePoint at);

// How far the panel moves when `handle`, taken hold of at `pressed`, is
// dragged to `now`: along the arrow's axis only (the point of the axis
// nearest the mouse), or across the view in the plane through the centre
// that faces the camera. Rounded to `step` cm. Empty when it cannot be
// worked out (the axis points straight along the mouse's ray).
std::optional<sim::Vec3> moveBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                                PicturePoint now, double step);

// Adds the gizmo to the scene's overlay; `lit` is drawn in the selection colour.
void addGizmo(Scene& scene, const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, std::optional<GizmoHandle> lit);

// Which way x, y and z point, as small arrows near the bottom left corner of
// the picture (x red, y green, z blue), whatever the camera does.
void addCornerAxes(Scene& scene, const OrbitCamera& camera, ViewSize size, double pixel);

} // namespace clothcad::render
