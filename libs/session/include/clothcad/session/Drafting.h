#pragma once
// What a person does on the table, and what it changes in the document: the
// rules every window shares. A window's table only reports a gesture (a
// corner dragged, an edge clicked with the Point tool); Drafting decides,
// through the editor, so that every change can be undone and one that would
// break the pattern is refused. It also keeps what is selected and the drags
// in progress.
//
//   Drafting drafting;
//   const Outcome done = drafting.handle(editor, MoveCorner{panel, corner, 2.0, 0.0, DragPhase::Begin});
//   if (!done.notice.empty()) say(done.notice);
//
// On a symmetric panel (a mirror applied last), what is done on the mirrored
// side is done on the drawn side, the mirrored way.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "clothcad/core/Document.h"
#include "clothcad/core/Drawing.h"
#include "clothcad/core/Fields.h"
#include "clothcad/session/Editor.h"

namespace clothcad::session {

// A drag is reported every time it moves: once as Begin, then Continue,
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
    double dx, dy;  // how far it has been dragged since the drag began, on the table
    DragPhase phase;
};
struct MoveHandle {
    PanelId panel;
    EdgeRef edge;
    bool endHandle;  // the handle at the edge's end, not its start
    double dx, dy;   // how far it has been dragged since the drag began, on the table
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
// A click on an edge with the Point tool: a corner goes there.
struct AddPointOn {
    PanelId panel;
    OutlinePoint at;
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
// A double-click on an edge's length: the user wants to type it.
struct EditLength {
    PanelId panel;
    EdgeRef edge;
};

using Gesture = std::variant<SelectPanel, MovePanel, MoveCorner, MoveHandle, DrawRect, DrawCircle, PlaceRing, BendEdge,
                             DrawLine, CutAway, SplitAt, MirrorAcross, EditLength, RotatePanel, SelectCorners, SewEdges, AddPointOn>;

// What a gesture came to, beyond the document: words for the help line, and
// what the window should do next.
struct Outcome {
    std::string notice{};       // empty: nothing to say
    bool refused = false;       // the notice says why nothing changed
    bool toolDone = false;      // the tool has done its work: back to Select
    struct LengthField {        // EditLength: the field that is this edge's length
        OpId operation;
        FieldId field;
        std::string label;      // "Width of 1. Rectangle"
    };
    std::optional<LengthField> typeLength{};
};

class Drafting {
public:
    Outcome handle(Editor& editor, const Gesture& gesture);
    // A new document, or undo and redo: drags in progress are over, and a
    // selection that no longer exists goes.
    void forget(const Document& doc);

    // What is selected. Windows read and set it as they please.
    struct Selection {
        std::optional<PanelId> panel;
        std::vector<CornerRef> corners;  // of that panel
    };
    Selection selection;

    // "panel1", "panel2" ...: the first panel name that is free.
    static std::string freeName(const Document& doc, const std::string& base);

private:
    Outcome on(Editor&, const SelectPanel&);
    Outcome on(Editor&, const MovePanel&);
    Outcome on(Editor&, const MoveCorner&);
    Outcome on(Editor&, const MoveHandle&);
    Outcome on(Editor&, const RotatePanel&);
    Outcome on(Editor&, const SelectCorners&);
    Outcome on(Editor&, const DrawRect&);
    Outcome on(Editor&, const DrawCircle&);
    Outcome on(Editor&, const PlaceRing&);
    Outcome on(Editor&, const BendEdge&);
    Outcome on(Editor&, const AddPointOn&);
    Outcome on(Editor&, const DrawLine&);
    Outcome on(Editor&, const CutAway&);
    Outcome on(Editor&, const SplitAt&);
    Outcome on(Editor&, const MirrorAcross&);
    Outcome on(Editor&, const SewEdges&);
    Outcome on(Editor&, const EditLength&);

    // The operation a drag writes to, and its values when the drag began.
    struct Dragging {
        OpId operation;
        bool endHandle;
        double baseX, baseY;
    };
    std::optional<Dragging> dragging_;
    struct CornerDrag {
        CornerRef corner;  // the drawn original that moves
        OpId operation;    // the adjustment it is written to
    };
    std::vector<CornerDrag> cornerDrags_;  // while a group of corners is dragged
    // A panel being turned by its knob turns about the table position of its box's centre.
    struct Turning {
        Vec2 centre;  // in its own coordinates
        Vec2 pivot;   // where that is on the table
    };
    std::optional<Turning> turning_;
};

} // namespace clothcad::session
