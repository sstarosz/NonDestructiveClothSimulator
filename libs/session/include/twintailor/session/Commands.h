#pragma once
// What can be done to the work, each as a plain struct that holds all it
// needs: no selection, no tool, no drag in progress. A Session runs them (its
// Editor does the work on the document). A command is made on a copy of the
// document: one that cannot be done changes nothing. A command run by itself is
// one undo step:
//
//   auto made = session.run(DuplicatePanels{.panels = {front}, .how = Follow::MirroredInstance});
//   if (!made) say(made.error());
//   else select(made->panels);
//
// What takes several commands, or follows the mouse, is done in an edit: all of
// it is one undo step, kept or taken back whole. In an edit, `run` adds a
// command to it, and `preview` shows a command in place of the preview before
// it, so a command that follows the mouse says how far the mouse has come in all:
//
//   const Edit edit = session.begin();
//   session.preview(edit, MoveCorners{.panel = front, .corners = {corner}, .by = {1, 0}});
//   session.preview(edit, MoveCorners{.panel = front, .corners = {corner}, .by = {3, 0}});  // 3 cm in all
//   session.commit(edit);  // it stays; or session.cancel(edit): as if it never was
//
// One edit is open at a time. A command run by itself, undo, redo or another
// begin ends it first, and what it made stays. A call with an edit that is over
// changes nothing.
//
// A new command is a struct here, with what it hands back (Done), how it may
// change the document (rules) and what it does (apply), and a place in
// DocumentCommand.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Fields.h"
#include "twintailor/session/Offers.h"
#include "twintailor/sim/Vec3.h"

namespace twintailor::session {

// An edit that was begun (Session::begin): what is run in it is one undo step.
struct Edit {
    std::uint64_t number = 0;
    friend bool operator==(const Edit&, const Edit&) = default;
};

// ---- How a change may change the document ----------------------------------------

// What a change may do to the document's problems.
enum class IfItBreaks {
    Refuse,  // a change that adds a problem is not made
    Allow,   // made anyway: adding or removing things, where a problem is the honest result
};

// What a change touches. Moving a panel on the table or in space, or putting on
// another avatar, changes no outline, so the outlines are not evaluated again.
enum class Touches { Shapes, PlacementOnly };

// Which way a panel grows when a change reshapes it.
enum class Grows {
    // Its sewn edges stay where they are, on the table and in space: a field or
    // a formula made a skirt longer, and it grows at the hem.
    AwayFromSeams,
    // Exactly as dragged: the panel's own coordinates stay put, on the table
    // and in space, so the corner under the mouse follows the mouse.
    WhereDragged,
    // For a length typed on the table: a panel without seams stays as
    // WhereDragged, so the end of the edge that is kept stays where it is on the
    // screen; a sewn panel grows away from its seams.
    InPlaceUnlessSewn,
};

struct ChangeRules {
    IfItBreaks ifItBreaks = IfItBreaks::Refuse;
    Touches touches = Touches::Shapes;
    Grows grows = Grows::AwayFromSeams;
};

// What a command hands back when there is nothing to tell.
struct Nothing {};

// ---- Commands on the document ----------------------------------------------------

// A formula field of an operation.
struct OperationField {
    OpId operation;
    FieldId field;
};

// The numbers of a panel's place on the table, and of its place in space
// (Arrangement).
enum class TableField { X, Y, Angle };
enum class SpaceField { X, Y, Z, Turn, Tilt, Roll, Wrap };
struct PanelField {
    PanelId panel;
    std::variant<TableField, SpaceField> field;
};

// A parameter's value, a field of an operation, or a number of a panel's place,
// set to a number or a formula. A place keeps plain numbers: a formula for it is
// worked out once. Setting a number in space gives the panel a place there.
struct SetField {
    std::variant<Param, OperationField, PanelField> field;
    Expr value;
    Grows grows = Grows::AwayFromSeams;  // how the panels it reshapes grow

    using Done = Nothing;
    ChangeRules rules() const {
        if (std::holds_alternative<PanelField>(field)) return {.touches = Touches::PlacementOnly};
        return {.grows = grows};
    }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Corners of a panel moved by as much, on the table. On a symmetric panel a
// mirrored corner moves its original, the mirrored way. A corner that a
// formula places stays; if all of them do, the command says why. Each corner
// has an adjustment of its own, and keeps being moved by it while nothing comes
// after it but adjustments of other corners (and layers that are off, and a
// mirror applied last); then it gets a new one.
struct MoveCorners {
    PanelId panel;
    std::vector<CornerRef> corners;
    Vec2 by;  // on the table, in cm

    using Done = Nothing;
    ChangeRules rules() const { return {.grows = Grows::WhereDragged}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// How a panel added next to another relates to it: a copy of its own, or
// something that follows the source (an instance, or its mirror image).
enum class Follow { Copy, Instance, MirroredInstance };

// A new panel like each of `panels`. A group keeps its shape (mirrored
// instances are laid out as its mirror image), with the middle of its box at
// `at`, or just right of where it is. Instances are sewn as their sources are,
// as far as there is something to sew them to (Document::sewAsSource). A
// mirrored instance of a panel at one side of the body also stands in space as
// its mirror image across x = 0, if the source has a place there and its anchor
// is at least a quarter of its width from the middle; everything else stands
// where the table has it. This is the new panel's first place: it does not
// follow the source's later moves.
struct DuplicatePanels {
    std::vector<PanelId> panels;
    Follow how = Follow::Copy;
    std::optional<Vec2> at{};  // on the table

    struct Done {
        std::vector<PanelId> panels;  // the new ones, in the order of their sources
        std::size_t seams = 0;        // how many they were sewn with
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Panels removed with what they hold. Whatever used one (an instance, a seam)
// stays and shows a problem.
struct DeletePanels {
    std::vector<PanelId> panels;

    using Done = Nothing;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Corners removed from a panel. A corner can only go with the operation that
// made it, and only one added as a point is only that corner: any other is part
// of a shape, and the command says which operation to change instead.
struct DeleteCorners {
    PanelId panel;
    std::vector<CornerRef> corners;

    using Done = Nothing;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// An edge given a length of its own, as long as it is now (a SetLength layer),
// where its shape has no setting for it or one that it shares with another edge.
// On a symmetric panel it is given to the drawn side. It keeps the end on the
// mirror of a symmetric panel, else both ends move.
struct GiveEdgeLength {
    PanelId panel;
    EdgeRef edge;

    struct Done {
        OpRef<SetLength> layer;
    };
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// The lengths given to an edge alone removed, so that its shape's setting gives
// it a length again; a layer that is off is kept. Refused if the edge has none
// of its own, or if the shape's setting would not give it a length, its corners
// having been moved.
struct RemoveEdgeLength {
    PanelId panel;
    EdgeRef edge;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Which end stays where it is when an edge is given its length.
struct SetKeeping {
    OpRef<SetLength> layer;
    SetLength::Keeping keeping;
    Grows grows = Grows::AwayFromSeams;

    using Done = Nothing;
    ChangeRules rules() const { return {.grows = grows}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// ---- Drawing and cutting ---------------------------------------------------------
//
// Each adds layers, so a problem it leaves behind is shown there, not refused.
// A point or an edge shown on the mirrored side of a symmetric panel stands for
// the drawn side's.

// A new panel with its starting shape: a rectangle has its bottom left corner at
// `at` on the table, a circle or a ring its centre. With no name it gets the
// first free one of its kind: "panel1", "circle1", "skirt1".
struct AddPanel {
    std::variant<Rect, Circle, RingSector> shape;
    Vec2 at;
    std::string name{};

    struct Done {
        PanelId panel;
        OpId shape;  // the starting layer: OpRef<Rect>(shape) names its edges
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// An edge made a free curve (a Bezier layer) that starts as the edge is: a
// straight one stays straight until a handle moves, an arc is followed. Refused
// for an edge that is a free curve already, and for a whole circle.
struct CurveEdge {
    PanelId panel;
    EdgeRef edge;

    struct Done {
        OpRef<Bezier> curve;
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A handle of a free curve moved by as much, on the table. A handle that a
// formula sets is refused, with why (handleLock).
struct MoveCurveHandle {
    PanelId panel;
    EdgeRef edge;
    bool endHandle;  // the handle at the edge's end, not its start
    Vec2 by;         // on the table, in cm

    using Done = Nothing;
    ChangeRules rules() const { return {.grows = Grows::WhereDragged}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A corner added on an edge (an AddPoint layer), splitting it in two.
struct AddCorner {
    PanelId panel;
    EdgePoint at;

    struct Done {
        CornerRef corner;
        bool splitsSeam = false;  // the edge was sewn: its seam now sews only the part before the corner
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// An internal line between two points of the outline (a LineBetween layer).
// Refused across the mirror of a symmetric panel.
struct AddLine {
    PanelId panel;
    EdgePoint from, to;

    struct Done {
        OpRef<LineBetween> line;
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A panel cut along a line, keeping one side.
struct CutPanel {
    PanelId panel;
    LineRef line;
    Cut::Side keep = Cut::Side::Left;

    struct Done {
        OpRef<Cut> cut;
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A panel cut along a line into two: it keeps what is left of the line, and a new
// panel, "<name> part1", is what the cut takes off it.
struct SplitPanel {
    PanelId panel;
    LineRef line;

    struct Done {
        PanelId part;
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A half panel completed by its mirror image across one of its edges.
struct AddMirror {
    PanelId panel;
    EdgeRef across;

    struct Done {
        OpRef<Mirror> mirror;
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// ---- Layers ----------------------------------------------------------------------

// A layer added at the end of a panel's, as the list offers it (Offers.h): it
// hands back the layer, and what it still needs to have chosen (SetReference).
// Until then it shows a problem.
struct AddLayer {
    PanelId panel;
    Offer offer;

    using Done = Added;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A layer removed. What used something it made shows a problem.
struct RemoveLayer {
    OpId layer;

    using Done = Nothing;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A layer switched on or off: one that is off stays, and is skipped.
struct SwitchLayer {
    OpId layer;
    bool on;

    using Done = Nothing;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A layer moved to place `to` among its panel's layers, the oldest being 0. The
// starting shape stays first.
struct MoveLayer {
    OpId layer;
    std::size_t to;

    using Done = Nothing;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// What a layer uses (an edge, a corner, a line, a panel) set to another.
struct SetReference {
    OpId layer;
    ReferenceId reference;  // from references(doc.get(layer))
    Reference value;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Which side of its line a cut keeps.
struct SetCutSide {
    OpRef<Cut> layer;
    Cut::Side keep;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Whether a mirror is applied after every other layer of its panel.
struct SetMirrorLast {
    OpRef<Mirror> layer;
    bool last;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Whether an instance is the mirror image of its source.
struct SetInstanceMirrored {
    OpRef<Instance> layer;
    bool mirrored;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Every formula of a panel's layers replaced by the number it comes to. Refused,
// saying so, when there is none.
struct BakePanel {
    PanelId panel;

    struct Done {
        std::size_t replaced = 0;
    };
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// ---- Seams -----------------------------------------------------------------------

// Two edges sewn together. Refused if they are sewn together already, either
// way round.
struct AddSeam {
    Seam seam;

    struct Done {
        SeamId seam;
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Whether a seam sews start to start (Seam::reversed), which twists the two
// pieces against each other.
struct SetSeamReversed {
    SeamId seam;
    bool reversed;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

struct RemoveSeam {
    SeamId seam;

    using Done = Nothing;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Instances sewn as the panels they follow are sewn, for seams made on a source
// after its instance (Document::sewAsSource). Panels that follow none are left.
// Refused, saying why, if there is nothing to add.
struct SewInstances {
    std::vector<PanelId> instances;

    struct Done {
        std::size_t seams = 0;  // how many were added
    };
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// ---- Panel settings, parameters and the avatar -----------------------------------

// A name of spaces only is refused.
struct RenamePanel {
    PanelId panel;
    std::string name;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Whether a panel is only a base for instances, not a piece cut from cloth.
struct SetConstruction {
    PanelId panel;
    bool construction;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// How fine a panel's cloth is meshed: a plain number from finestParticleDistance
// to coarsestParticleDistance, in cm.
struct SetParticleDistance {
    PanelId panel;
    Expr distance;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A new parameter. With no name it gets the first free of "p1", "p2" ...; a
// name must be one that formulas can use and that no other parameter has.
struct AddParameter {
    std::string name{};
    Expr value = 10_cm;
    Unit unit = Unit::Length;

    struct Done {
        Param parameter;
    };
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A parameter renamed, by the rules of AddParameter; formulas follow it.
struct RenameParameter {
    Param parameter;
    std::string name;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A parameter removed. What used it shows a problem.
struct RemoveParameter {
    Param parameter;

    using Done = Nothing;
    ChangeRules rules() const { return {.ifItBreaks = IfItBreaks::Allow}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// The body the pattern is for (Document::setAvatar); none takes it off.
struct SetAvatar {
    std::shared_ptr<const Avatar> avatar;

    using Done = Nothing;
    ChangeRules rules() const { return {.touches = Touches::PlacementOnly}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// ---- Placing panels --------------------------------------------------------------

// The middle of the box that holds these panels where the table has them: what a
// group turns and is scaled about, and where its gizmo stands. Empty if none has
// an outline.
std::optional<Vec2> middleOfPanels(const Document& doc, const Result& shapes, const std::vector<PanelId>& panels);

// Panels moved on the table, each by as much. Their places in space stay.
struct MovePanels {
    std::vector<PanelId> panels;
    Vec2 by;  // in cm

    using Done = Nothing;
    ChangeRules rules() const { return {.touches = Touches::PlacementOnly}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Panels turned on the table by as many degrees: one about the middle of its own
// box, several about the middle of their group (middleOfPanels).
struct TurnPanels {
    std::vector<PanelId> panels;
    double degrees;

    using Done = Nothing;
    ChangeRules rules() const { return {.touches = Touches::PlacementOnly}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Panels made as many times as big, along each one's own x and y: 1 is as it
// is. What changes depends on the panel:
//   - a Scale layer at its end, if one is there and holds plain numbers (a layer
//     the user added stays the one that is changed);
//   - else the lengths of its starting shape, if that is a Rect, a Circle or a
//     RingSector whose lengths are plain numbers (so no layer is added);
//   - else a new Scale layer at the end (a formula that sets a length is never
//     overwritten, and an Instance has no lengths of its own).
// A Scale layer set by a formula refuses the command and says where to change it.
// Whichever way, the middle of a single panel's box stays where it was on the
// table (the panel's place moves to keep it there) and in space (its anchor
// moves by as much as that middle moved in the panel's own coordinates).
// Several panels are scaled evenly (x and y the same): on the table about the
// middle of the group, each panel's middle moving away from it by as much; in
// space each about its own place.
struct ScalePanels {
    std::vector<PanelId> panels;
    double x = 1.0, y = 1.0;

    using Done = Nothing;
    ChangeRules rules() const { return {.grows = Grows::WhereDragged}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A panel carried through space, by as much in cm. One without a place of its
// own starts from where the table has it. Its shape stays.
struct MovePanelInSpace {
    PanelId panel;
    sim::Vec3 by;

    using Done = Nothing;
    ChangeRules rules() const { return {.touches = Touches::PlacementOnly}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A panel turned in space: about which axis, how many degrees
// (counter-clockwise looking down the axis), about an axis through `about`,
// else through its anchor. It starts as MovePanelInSpace does.
struct TurnPanelInSpace {
    PanelId panel;
    sim::Vec3 axis;
    double degrees;
    std::optional<sim::Vec3> about{};

    using Done = Nothing;
    ChangeRules rules() const { return {.touches = Touches::PlacementOnly}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// A panel's place in space given up, so that it gets one again from where the
// table has it now (sim::firstPlace). Its anchor is kept.
struct PlaceFromTable {
    PanelId panel;

    using Done = Nothing;
    ChangeRules rules() const { return {.touches = Touches::PlacementOnly}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Every command that changes the document.
using DocumentCommand = std::variant<SetField, MoveCorners, DuplicatePanels, DeletePanels, DeleteCorners, GiveEdgeLength, RemoveEdgeLength, SetKeeping,
                                     MovePanels, TurnPanels, ScalePanels, MovePanelInSpace, TurnPanelInSpace, PlaceFromTable, AddPanel, CurveEdge,
                                     MoveCurveHandle, AddCorner, AddLine, CutPanel, SplitPanel, AddMirror, AddLayer, RemoveLayer, SwitchLayer,
                                     MoveLayer, SetReference, SetCutSide, SetMirrorLast, SetInstanceMirrored, BakePanel, AddSeam, SetSeamReversed,
                                     RemoveSeam, SewInstances, RenamePanel, SetConstruction, SetParticleDistance, AddParameter, RenameParameter,
                                     RemoveParameter, SetAvatar>;

template <class C>
concept DocumentChange = isAlternative<C, DocumentCommand>;

// ---- Commands on the cloth -------------------------------------------------------

// A particle of the cloth shown, taken hold of and led to `to`. Only a Session
// runs it, and only as a preview: it changes no document, and the whole edit is
// one undo step, which brings back the drape as it was before.
//
//   const Edit pull = session.begin();
//   session.preview(pull, PullCloth{panel, point, to});  // takes hold
//   session.preview(pull, PullCloth{panel, point, further});
//   session.commit(pull);                                // lets go; or cancel(pull)
struct PullCloth {
    PanelId panel;
    std::uint32_t point;  // indexes the panel's particles in Session::cloth().mesh
    sim::Vec3 to;
};

// The cloth taken off: the panels are back where they are arranged, and nothing
// runs until the cloth is simulated again. Only a Session runs it. It changes no
// document and is one undo step: undo brings the drape back as it hung, and redo
// takes it off again.
//
//   session.run(ResetCloth{});
//   session.undo();  // the drape is back at the next update()
struct ResetCloth {};

} // namespace twintailor::session
