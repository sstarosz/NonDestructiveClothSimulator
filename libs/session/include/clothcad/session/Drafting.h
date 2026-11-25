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

#include <expected>
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
// The panel dragged moves to (x, y); if it is one of several selected panels, they
// all move by as much, as one undo step.
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
// If it is one of several selected panels, they all turn by as much, about the
// middle of the group (middleOfPanels).
struct RotatePanel {
    PanelId panel;
    double angle;
    DragPhase phase;
};
// A panel scaled by the scale gizmo, along its own x and y: how many times as
// big as when the drag began (1 is as it was). What changes depends on the panel:
//   - a Scale layer at its end, if one is there and holds plain numbers (a layer
//     the user added stays the one that is changed);
//   - else the lengths of its starting shape, if that is a Rect, a Circle or a
//     RingSector whose lengths are plain numbers (so no layer is added);
//   - else a new Scale layer at the end (a formula that sets a length is never
//     overwritten, and an Instance has no lengths of its own).
// A Scale layer set by a formula refuses the drag and says where to change it.
// Whichever way, the middle of the panel's box stays where it was on the table
// (the panel's place moves to keep it there). If it is one of several selected
// panels, they are all scaled by as much, evenly (x and y the same), about the
// middle of the group: each panel's middle moves away from it by as much.
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
// A double-click on an edge's length: the user wants to type it. With
// `ownLength` (the Length tool's click) the edge gets a length of its own even
// where its shape has a setting for it, as a rectangle's top shares its width
// with the bottom; that is how a trousers' waist and ankle come to differ.
struct EditLength {
    PanelId panel;
    EdgeRef edge;
    bool ownLength = false;
};
// The edge's own length is given up: it is the shape's setting again (a rectangle's
// width or height), and the length that was given to the edge alone is removed.
struct UseShapeLength {
    PanelId panel;
    EdgeRef edge;
};

// How a panel added next to another relates to it: a copy of its own, or
// something that follows the source (an instance, or its mirror image).
enum class Follow { Copy, Instance, MirroredInstance };
// A new panel just right of `source`, selected. An instance is sewn as its
// source is, as far as there is something to sew it to (Document::sewAsSource).
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
// What Ctrl+D and the shelf's instance buttons do: the selected panels each get
// a copy (or an instance, or a mirrored instance) just right of the group,
// keeping their layout; for mirrored instances the layout is mirrored too. The
// new panels become the selection. Instances are sewn as their sources are, so
// a front and a back sewn together give two instances sewn together. One undo step.
struct DuplicateSelection {
    Follow how = Follow::Copy;
};
// The selected instances sewn as the panels they follow are sewn, for seams made
// on a source after its instance (Document::sewAsSource). One undo step.
struct SewAsSource {};
// The copied panels, copied again as a group with its middle at `at` on the
// table, each keeping its place in the group; with no place (the 3D view has
// none), just right of the originals. The copies are selected.
struct PastePanel {
    std::optional<Vec2> at;
};

using Gesture = std::variant<SelectPanel, MovePanel, MoveCorner, MoveHandle, DrawRect, DrawCircle, PlaceRing, BendEdge,
                             DrawLine, CutAway, SplitAt, MirrorAcross, EditLength, UseShapeLength, RotatePanel, SelectCorners, SewEdges, AddPointOn,
                             ScalePanel, MoveInSpace, TurnInSpace, AddBeside, DeletePanel, DeleteSelection, CopySelection, DuplicateSelection, PastePanel, SelectPanels,
                             SewAsSource>;

// What a gesture came to, beyond the document: words for the help line, and
// what the window should do next.
struct Outcome {
    std::string notice{};       // empty: nothing to say
    bool refused = false;       // the notice says why nothing changed
    bool toolDone = false;      // the tool has done its work: Drafting is back to Select
    struct LengthField {        // EditLength: the field that is this edge's length
        OpId operation;
        FieldId field;
        EdgeRef edge;           // the edge, so that a window can type on its label
        std::string label;      // "Width of 1. Rectangle"
        std::string caption;    // "Width of top edge": what a window names the field by
        bool ownLength = false;  // the field is a length given to this edge alone (SetLength)
        bool canSwitch = false;  // the edge also has a setting of its shape: the window offers the other one
        std::string shapeSetting;  // what the shape calls that setting ("Width"), when canSwitch
        std::optional<SetLength::Keeping> keeping{};  // of an own length: which end stays; the window shows it
    };
    std::optional<LengthField> typeLength{};
};

// The middle of the box that holds these panels where the table has them: what a
// group turns about, and where its gizmo stands. Empty if none has an outline.
std::optional<Vec2> middleOfPanels(const Document& doc, const Result& shapes, const std::vector<PanelId>& panels);

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
    Outcome on(Editor&, const UseShapeLength&);
    Outcome on(Editor&, const MoveInSpace&);
    Outcome on(Editor&, const TurnInSpace&);
    Outcome on(Editor&, const AddBeside&);
    Outcome on(Editor&, const DeletePanel&);
    Outcome on(Editor&, const DeleteSelection&);
    Outcome on(Editor&, const CopySelection&);
    Outcome on(Editor&, const DuplicateSelection&);
    Outcome on(Editor&, const PastePanel&);
    Outcome on(Editor&, const SewAsSource&);
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
    // Panels being scaled by the gizmo, each in the way that adds the least to the
    // document (see ScalePanel). A layer is only added with the first change that is
    // accepted, so a refused drag leaves nothing behind.
    struct ScalePart {
        PanelId panel;
        std::optional<OpId> layer;  // the Scale layer changed or added; empty until added
        std::optional<OpId> shape;  // the starting operation whose lengths are scaled instead of a layer
        double baseA = 1.0, baseB = 1.0;  // the layer's x and y, or the shape's two lengths, when the drag began
        Vec2 middle;                // the middle of its box on the table when the drag began
    };
    struct Scaling {
        std::vector<ScalePart> parts;
        Vec2 pivot;                 // what a group is scaled about
        bool several = false;
        bool stepOpen = false;      // whether an accepted change has opened the undo step
    };
    std::optional<Scaling> scaling_;
    std::expected<ScalePart, std::string> scalePartOf(const Editor& editor, PanelId panel) const;
    // A panel being turned by the gizmo turns about the table position of its box's centre.
    struct Turning {
        Vec2 centre;  // in its own coordinates
        Vec2 pivot;   // where that is on the table
    };
    std::optional<Turning> turning_;
    // Several selected panels moved or turned together: where each was when the drag began.
    struct GroupStart {
        PanelId panel;
        double x, y, angle;
    };
    std::vector<GroupStart> groupStarts_;
    Vec2 groupPivot_;  // what a group turns about
    // Fills groupStarts_ if `panel` is one of several selected ones; says whether it did.
    bool beginGroup(const Document& doc, PanelId panel);
    std::optional<Arrangement> arrangedFrom_;  // a panel's arrangement when a drag in space began
    std::vector<PanelId> copied_;              // what CopySelection remembered
};

} // namespace clothcad::session
