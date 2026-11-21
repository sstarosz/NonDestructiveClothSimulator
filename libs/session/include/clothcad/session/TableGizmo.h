#pragma once
// The gizmo on the selected panel on the table, as Maya has them, and what
// dragging its handles does. Q selects only and shows none; W moves: an arrow
// along the table's x (red) and one along its y (green), and the square
// between them moves freely; E turns: a ring (blue, as z, the table's
// upright axis) about the middle of the panel's box; R scales: a handle
// along the panel's own x and y, and the square between them (blue, as in
// Maya) and the one in the middle scale evenly.
//
// It stands at the middle of the panel's box and is as big on the screen at
// any zoom. Everything is in table cm; `cmPerPixel` says how big one screen
// pixel is. The 3D view's gizmos (render/Gizmo) take the same modes.
//
//   const session::TableGizmo gizmo{.mode = GizmoMode::Move, .centre = pivot, .angle = panel.angle, .cmPerPixel = 1.0 / scale};
//   if (const std::optional<session::TableHandle> held = session::handleAt(gizmo, pressed))
//       Vec2 by = session::moveBy(gizmo, *held, pressed, now);

#include <array>
#include <optional>

#include "clothcad/core/Pattern.h"
#include "clothcad/session/Tools.h"

namespace clothcad::session {

enum class TableHandle {
    Middle,  // Move: anywhere on the table; Scale: evenly
    AlongX,  // Move: the table's x; Scale: the panel's own x
    AlongY,  // Move: the table's y; Scale: the panel's own y
    Plane,   // Scale: the square between x and y, both evenly
    Ring,    // Rotate
};

struct TableGizmo {
    GizmoMode mode = GizmoMode::Move;
    Vec2 centre;             // the middle of the panel's box, on the table
    double angle = 0.0;      // the panel's angle on the table: the scale handles follow its own axes
    double cmPerPixel = 1.0;
};

// Its size on the screen, in pixels.
inline constexpr double tableArrowPixels = 110.0;  // an arrow or scale handle, from the middle
inline constexpr double tableRingPixels = 90.0;    // the rotate ring's radius
inline constexpr double tableMiddlePixels = 8.0;   // half the middle square
inline constexpr double tablePlanePixels = 10.0;   // half the square between x and y; its middle is 0.3 of an arrow out along each
inline constexpr double tableReachPixels = 9.0;    // how near a handle the pointer must be to take it

// Where an arrow or a scale handle ends (AlongX, AlongY), on the table.
Vec2 handleEnd(const TableGizmo& gizmo, TableHandle along);
// The corners of the scale gizmo's square between x and y, in order, on the table.
std::array<Vec2, 4> planeCorners(const TableGizmo& gizmo);

// The handle under a table point, if any: the middle square first, then the
// square between x and y (Scale), then the nearest arrow; for Rotate, the ring.
std::optional<TableHandle> handleAt(const TableGizmo& gizmo, Vec2 at);

// How far the panel moves when `handle`, taken hold of at `pressed`, is
// dragged to `now`: along that axis only, or freely for the middle.
Vec2 moveBy(const TableGizmo& gizmo, TableHandle handle, Vec2 pressed, Vec2 now);

// How many degrees, counter-clockwise, the ring has been turned about the centre.
double turnBy(const TableGizmo& gizmo, Vec2 pressed, Vec2 now);

// How many times as big along the panel's own x and y: a handle by how much
// farther from the middle the mouse is along it, the square between x and y
// the same along the line between them, for both; the middle evenly, a
// hundredth more for every pixel dragged right, less to the left. Never
// below a hundredth.
struct Factors {
    double x = 1.0, y = 1.0;
};
Factors scaleBy(const TableGizmo& gizmo, TableHandle handle, Vec2 pressed, Vec2 now);

} // namespace clothcad::session
