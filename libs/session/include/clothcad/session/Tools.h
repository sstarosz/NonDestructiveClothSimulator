#pragma once
// The tools a person works with, the same in every window and both views:
// Select (Q), Move (W), Rotate (E) and Scale (R) select and work on a panel
// in 2D and 3D alike, as in Maya; the others draw on the table. Which one is
// in use is kept by session::Drafting, with the selection.
//
//   drafting.tool = session::Tool::Rotate;
//   if (const std::optional<session::GizmoMode> mode = session::gizmoOf(drafting.tool)) showGizmo(*mode);

#include <optional>

namespace clothcad::session {

enum class Tool { Select, Move, Rotate, Scale, Rectangle, Circle, Ring, Point, Curve, Line, Length, Mirror, Cut, Split, Sew, Pull };

// What a gizmo does, in both views: W, E and R.
enum class GizmoMode { Move, Rotate, Scale };

// The gizmo a tool shows on the selected panel; none for Select and the drawing tools.
constexpr std::optional<GizmoMode> gizmoOf(Tool tool) {
    switch (tool) {
    case Tool::Move: return GizmoMode::Move;
    case Tool::Rotate: return GizmoMode::Rotate;
    case Tool::Scale: return GizmoMode::Scale;
    default: return std::nullopt;
    }
}

} // namespace clothcad::session
