#pragma once
// The canvas tools, and the gestures the canvas reports. The canvas only
// says what the user did; the App decides what that changes in the document.

#include <optional>
#include <variant>
#include <vector>

#include "core/Document.h"
#include "core/Drawing.h"

namespace clothcad::app {

enum class Tool { Select, Rectangle, Circle, RingSector, Curve, Line, Cut, Split, Mirror, Sew };

// A drag is reported every frame it moves: once as Begin, then Continue,
// and End when the button is released.
enum class DragPhase { Begin, Continue, End };

struct SelectPanel {
    std::optional<PanelId> panel;  // empty: clicked on nothing
};
struct MovePanel {
    PanelId panel;
    double x, y;  // where the panel's origin should now be on the table
    DragPhase phase;
};
struct MoveCorner {
    PanelId panel;
    CornerRef corner;
    double dx, dy;  // how far it has been dragged since the drag began
    DragPhase phase;
};
struct MoveHandle {
    PanelId panel;
    EdgeRef edge;
    bool endHandle;  // the handle at the edge's end, not its start
    double dx, dy;   // how far it has been dragged since the drag began
    DragPhase phase;
};
// A panel turned by its knob: `angle` is where it now points, in degrees.
struct RotatePanel {
    PanelId panel;
    double angle;
    DragPhase phase;
};
// A click or a box dragged over corners of one panel. With `add` they join the
// corners already selected (a click on a selected one takes it out).
struct SelectCorners {
    PanelId panel;
    std::vector<CornerRef> corners;
    bool add;
};
struct DrawRect {
    double x, y, width, height;  // on the table; x, y is the bottom-left corner
};
struct DrawCircle {
    double x, y, radius;  // on the table; x, y is the centre
};
struct PlaceRing {
    double x, y;  // where the ring's centre goes
};
struct BendEdge {
    PanelId panel;
    EdgeRef edge;
};
struct DrawLine {
    PanelId panel;
    OutlinePoint from, to;
};
struct CutAway {
    PanelId panel;
    LineRef line;
    Cut::Side keep;
};
struct SplitAt {
    PanelId panel;
    LineRef line;
};
struct MirrorAcross {
    PanelId panel;
    EdgeRef edge;
};

// Two edges clicked with the Sew tool.
struct SewEdges {
    SeamSide a, b;
};

// A double-click on an edge's length label: the user wants to type it.
struct EditLength {
    PanelId panel;
    EdgeRef edge;
};

using Gesture = std::variant<SelectPanel, MovePanel, MoveCorner, MoveHandle, DrawRect, DrawCircle, PlaceRing, BendEdge,
                             DrawLine, CutAway, SplitAt, MirrorAcross, EditLength, RotatePanel, SelectCorners, SewEdges>;

} // namespace clothcad::app
