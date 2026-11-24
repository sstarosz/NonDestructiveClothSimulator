#pragma once
// What a person does on the table and in the 3D view, and what it changes in
// the document: the rules every window shares. A view only reports a gesture
// (a corner dragged, an edge clicked with the Point tool, a panel turned by
// the gizmo in 3D); Drafting decides, through the editor, so that every
// change can be undone and one that would break the pattern is refused. It
// also keeps the tool in use, what is selected and the drags in progress.
//
//   Drafting drafting;
//   const Outcome done = drafting.handle(editor, MoveCorner{panel, corner, 2.0, 0.0, DragPhase::Begin});
//   if (!done.notice.empty()) say(done.notice);
//
// On a symmetric panel (a mirror applied last), what is done on the mirrored
// side is done on the drawn side, the mirrored way.

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "clothcad/core/Document.h"
#include "clothcad/core/Drawing.h"
#include "clothcad/core/Fields.h"
#include "clothcad/session/Editor.h"
#include "clothcad/session/Tools.h"
#include "clothcad/sim/Vec3.h"

namespace clothcad::session {

// A drag is reported every time it moves: once as Begin, then Continue,
// and End when the button is released.
enum class DragPhase { Begin, Continue, End };

// How what a click or a box picks joins what is selected already.
enum class Pick {
    Replace,  // only these are selected
    Add,      // these join the others; one already selected stays
    Toggle,   // each of these goes in, or out if it was in (Shift-click on a corner, Ctrl-click on a panel)
};
// A click on a panel. With Add or Toggle it joins the panels selected already, and
// becomes the one the docks and the gizmo show.
struct SelectPanel {
    std::optional<PanelId> panel;  // empty: clicked on nothing
    Pick how = Pick::Replace;
};
// A box dragged over panels. Replace with none inside chooses nothing.
struct SelectPanels {
    std::vector<PanelId> panels;
    Pick how;
};
struct MovePanel {
    PanelId panel;
    double x, y;  // where the panel's origin should now be on the table
    DragPhase phase;
};
// Which way a dragged corner may go on the table: both ways, or only along its x or its y.
enum class Along { Both, X, Y };
// The axis a drag has gone furthest along (X when it went further sideways than up or down;
// a tie is X). It is for a window to decide once, when a drag with Shift has gone far enough.
Along axisOf(double dx, double dy);

struct MoveCorner {
    PanelId panel;
    CornerRef corner;
    double dx, dy;  // how far it has been dragged since the drag began, on the table
    DragPhase phase;
    Along along = Along::Both;  // X or Y: the other coordinate does not change at all
};
struct MoveHandle {
    PanelId panel;
    EdgeRef edge;
    bool endHandle;  // the handle at the edge's end, not its start
    double dx, dy;   // how far it has been dragged since the drag began, on the table
    DragPhase phase;
};
// A panel turned by the rotate gizmo: `angle` is where it now points, in degrees.
struct RotatePanel {
    PanelId panel;
    double angle;
    DragPhase phase;
};
// A panel scaled by the scale gizmo, along its own x and y: how many times as
// big as when the drag began (1 is as it was). It is a Scale layer at the end
// of the panel: the last one, if it is already there and holds plain
// numbers; else a new one.
struct ScalePanel {
    PanelId panel;
    double x, y;
    DragPhase phase;
};
// A click or a box dragged over corners of one panel.
struct SelectCorners {
    PanelId panel;
    std::vector<CornerRef> corners;
    Pick how;
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
// The selected panel carried through space by the move gizmo in 3D, or by
// itself: how far, in cm, since the drag began. It only places the panel.
struct MoveInSpace {
    PanelId panel;
    sim::Vec3 by;
    DragPhase phase;
};
// The selected panel turned in space by the rotate gizmo: about which axis,
// how many degrees since the drag began (counter-clockwise looking down the
// axis), about its anchor. It only places the panel.
struct TurnInSpace {
    PanelId panel;
    sim::Vec3 axis;
    double degrees;
    DragPhase phase;
};
// A double-click on an edge's length: the user wants to type it.
struct EditLength {
    PanelId panel;
    EdgeRef edge;
};

// How a panel added next to another relates to it: a copy of its own, or
// something that follows the source (an instance, or its mirror image).
enum class Follow { Copy, Instance, MirroredInstance };
// A new panel just right of `source`, selected (Duplicate, and the shelf's instance buttons).
struct AddBeside {
    PanelId source;
    Follow how;
};
// A panel removed with what it holds. Whatever used it (an instance, a seam)
// stays and shows a problem; undo brings everything back.
struct DeletePanel {
    PanelId panel;
};
// What Delete does: the selected corners that were added with the Point tool
// go (their layer is dropped); with no corner selected, the selected panels go,
// all in one undo step.
struct DeleteSelection {
    bool wholePanels = false;  // ignore selected corners: the 3D view shows none
};
// The selected panels are what Paste will copy: only remembered, nothing in the document changes.
struct CopySelection {};
// The copied panels, copied again as a group with its middle at `at` on the
// table, each keeping its place in the group; with no place (the 3D view has
// none), just right of the originals. The copies are selected.
struct PastePanel {
    std::optional<Vec2> at;
};

using Gesture = std::variant<SelectPanel, MovePanel, MoveCorner, MoveHandle, DrawRect, DrawCircle, PlaceRing, BendEdge,
                             DrawLine, CutAway, SplitAt, MirrorAcross, EditLength, RotatePanel, SelectCorners, SewEdges, AddPointOn,
                             ScalePanel, MoveInSpace, TurnInSpace, AddBeside, DeletePanel, DeleteSelection, CopySelection, PastePanel, SelectPanels>;

// What a gesture came to, beyond the document: words for the help line, and
// what the window should do next.
struct Outcome {
    std::string notice{};       // empty: nothing to say
    bool refused = false;       // the notice says why nothing changed
    bool toolDone = false;      // the tool has done its work: Drafting is back to Select
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

    // What is selected. Windows read and set it as they please. `panel` is the
    // one the docks show, the gizmo stands at and the corners belong to; `others`
    // are the panels selected with it (Ctrl-click, a box with Ctrl).
    struct Selection {
        std::optional<PanelId> panel;
        std::vector<CornerRef> corners;  // of `panel`
        std::vector<PanelId> others;
        // Every selected panel, `panel` first.
        std::vector<PanelId> panels() const;
        bool has(PanelId candidate) const;
    };
    Selection selection;
    // The tool in use. Windows set it; a drawing tool that has done its work
    // goes back to Select by itself.
    Tool tool = Tool::Select;
    // The panels Paste would copy, if any were copied. A new Drafting (a new
    // document) has none.
    const std::vector<PanelId>& copied() const { return copied_; }

    // "panel1", "panel2" ...: the first panel name that is free.
    static std::string freeName(const Document& doc, const std::string& base);

private:
    Outcome on(Editor&, const SelectPanel&);
    Outcome on(Editor&, const SelectPanels&);
    Outcome on(Editor&, const MovePanel&);
    Outcome on(Editor&, const MoveCorner&);
    Outcome on(Editor&, const MoveHandle&);
    Outcome on(Editor&, const RotatePanel&);
    Outcome on(Editor&, const ScalePanel&);
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
    Outcome on(Editor&, const MoveInSpace&);
    Outcome on(Editor&, const TurnInSpace&);
    Outcome on(Editor&, const AddBeside&);
    Outcome on(Editor&, const DeletePanel&);
    Outcome on(Editor&, const DeleteSelection&);
    Outcome on(Editor&, const CopySelection&);
    Outcome on(Editor&, const PastePanel&);
    // `panel` joins the selection as the one shown, or goes out of it; the others stay.
    void join(PanelId panel);
    void drop(PanelId panel);
    // A drag in space: the arrangement `now` works out from the one the panel had when the drag began.
    Outcome arrangeByDrag(Editor&, PanelId panel, DragPhase phase, const std::function<Arrangement(const Arrangement&)>& now);

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
    // A panel being scaled by the gizmo. The layer is only added with the first
    // change that is accepted, so a refused drag leaves nothing behind.
    struct Scaling {
        std::optional<OpId> layer;  // empty until the drag has added its own
        double baseX, baseY;
        bool stepOpen;              // whether an accepted change has opened the undo step
    };
    std::optional<Scaling> scaling_;
    // A panel being turned by the gizmo turns about the table position of its box's centre.
    struct Turning {
        Vec2 centre;  // in its own coordinates
        Vec2 pivot;   // where that is on the table
    };
    std::optional<Turning> turning_;
    std::optional<Arrangement> arrangedFrom_;  // a panel's arrangement when a drag in space began
    std::vector<PanelId> copied_;              // what CopySelection remembered
};

} // namespace clothcad::session
