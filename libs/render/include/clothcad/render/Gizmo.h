#pragma once
// The gizmo on the selected panel in the 3D view, as Maya draws them, and
// what dragging its handles does. x is red, y green, z blue; the handle held
// or under the mouse is amber.
//
//   W, Move: an arrow along each axis moves the panel along it, a square
//      between two arrows moves it in their plane, and the square in the
//      middle moves it across the view.
//   E, Rotate: a ring about each axis turns the panel about it, and the
//      outer ring turns it about the line of sight. Between the rings, on the
//      faint disc, it turns freely, as a ball: across the screen about the
//      screen's up, up and down about its side. As in Maya, a ring shows
//      (and is taken hold of) only where it is not behind the gizmo: seen
//      edge on a little more than its front half, facing the eye all of it.
//   R, Scale: a handle along the panel's own x and y stretches it, the
//      square between them and the cube in the middle scale it evenly. Its
//      own z is drawn grey: a panel is flat, it has no thickness to scale.
//
// It stands at the point the panel's arrangement places (its x, y and z in
// space), or, while there is cloth, on the panel's cloth at the spot that
// point has in the flat pattern. It is turned as the panel is arranged, is drawn in front of everything in flat colours, without light and
// shade, and is the same size on the screen at any distance.
//
//   const std::optional<render::Gizmo> gizmo = render::gizmoOf(session, panel, session::GizmoMode::Rotate, devicePixelRatio);
//   std::optional<render::GizmoHandle> held = render::handleAt(*gizmo, camera, size, pressed);
//   std::optional<render::Turn> turn = render::turnBy(*gizmo, *held, camera, size, pressed, now, 0.5);
//   render::addGizmo(scene, *gizmo, camera, size, held);  // into scene.overlay

#include <optional>

#include "clothcad/render/Camera.h"
#include "clothcad/render/Picking.h"
#include "clothcad/render/Scene.h"
#include "clothcad/session/Session.h"
#include "clothcad/session/TableGizmo.h"

namespace clothcad::render {

// Its size on the screen, in pixels of the screen's own scale.
inline constexpr double gizmoArrowPixels = 125.0;     // an arrow or scale handle, from the middle
inline constexpr double gizmoRingPixels = 100.0;      // the radius of a ring about an axis
inline constexpr double gizmoViewRingPixels = 122.0;  // the radius of the ring about the line of sight

enum class GizmoHandle {
    Middle,                     // Move: across the view; Scale: evenly
    AlongX, AlongY, AlongZ,     // Move: along an axis; Scale: along the panel's own x or y (its z does nothing)
    PlaneYZ, PlaneXZ, PlaneXY,  // Move: in the plane of two axes; Scale: PlaneXY, the panel's own x and y evenly
    AboutX, AboutY, AboutZ,     // Rotate: about an axis
    AboutView,                  // Rotate: about the line of sight
    Free,                       // Rotate: inside the rings, freely, as a ball (Maya's trackball)
};

struct Gizmo {
    session::GizmoMode mode = session::GizmoMode::Move;
    sim::Vec3 centre;
    // The panel's own x and y in space: what the scale handles stretch along.
    sim::Vec3 ownX{1.0, 0.0, 0.0}, ownY{0.0, 1.0, 0.0};
    // Picture pixels per pixel of the screen's own scale, so that the gizmo
    // is as big, and as easy to take hold of, on a 4K display.
    double pixel = 1.0;
};

// The gizmo of a panel: where its arrangement places it (on its cloth, when
// there is some), turned as it is arranged. Empty when the panel has no cloth mesh to show.
std::optional<Gizmo> gizmoOf(const session::Session& session, PanelId panel, session::GizmoMode mode, double pixel);

// The handle drawn at a place in the picture, if any: the middle first, then
// a plane's square, then the nearest arrow, ring or handle. One pointing
// almost at the eye, or a plane seen within about 20° of edge on, is too thin
// to be shown or taken.
std::optional<GizmoHandle> handleAt(const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, PicturePoint at);

// How far the panel moves when a Move handle, taken hold of at `pressed`, is
// dragged to `now`: along the arrow's axis only (the point of the axis
// nearest the mouse), in a plane under the mouse, or across the view in the
// plane through the centre that faces the camera. Rounded to `step` cm.
// Empty when it cannot be worked out (the axis points straight along the
// mouse's ray) or for a handle of another mode.
std::optional<sim::Vec3> moveBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                                PicturePoint now, double step);

// How far a Rotate ring turns the panel: about which axis in space, by how
// many degrees (counter-clockwise looking down the axis at its tip), rounded
// to `step` degrees. A ring seen face on turns with the mouse going round
// the centre; one seen aslant, by how far the mouse moves along it. Free
// turns about the line in the screen square to the drag, by a radian for
// every ring's radius dragged, so the front of the ball follows the mouse.
struct Turn {
    sim::Vec3 axis;
    double degrees = 0.0;
};
std::optional<Turn> turnBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                           PicturePoint now, double step);

// How many times as big a Scale handle makes the panel along its own x and y:
// a handle by how much farther along it the mouse is from the middle (the
// square between x and y the same, along the line between them, for both),
// the middle evenly, a hundredth more for every pixel dragged right. Rounded
// to `step`, never below a hundredth. Empty for the z handle: it does nothing.
std::optional<session::Factors> scaleBy(const Gizmo& gizmo, GizmoHandle handle, const OrbitCamera& camera, ViewSize size, PicturePoint pressed,
                                        PicturePoint now, double step);

// Adds the gizmo to the scene's overlay; `lit` is drawn in the selection colour.
void addGizmo(Scene& scene, const Gizmo& gizmo, const OrbitCamera& camera, ViewSize size, std::optional<GizmoHandle> lit);

// Which way x, y and z point, as small arrows near the bottom left corner of
// the picture (x red, y green, z blue), whatever the camera does.
void addCornerAxes(Scene& scene, const OrbitCamera& camera, ViewSize size, double pixel);

} // namespace clothcad::render
