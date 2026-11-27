#pragma once
// What can be done to the work, each as a plain struct that holds all it
// needs: no selection, no tool, no drag in progress. A Session or an Editor
// runs them. A command is made on a copy of the document: one that cannot be
// done changes nothing.
//
//   auto made = editor.run(DuplicatePanels{.panels = {front}, .how = Follow::MirroredInstance});
//   if (!made) say(made.error());
//   else select(made->panels);
//
// Dragged, a command is applied each time to the document as it was when the
// drag began, so it says how far the drag has come in all:
//
//   editor.drag(MoveCorners{.panel = front, .corners = {corner}, .by = {1, 0}});
//   editor.drag(MoveCorners{.panel = front, .corners = {corner}, .by = {3, 0}});  // 3 cm in all
//   editor.endDrag();                                                            // one undo step; or cancelDrag()
//
// A new command is a struct here, with what it hands back (Done), how it may
// change the document (rules) and what it does (apply), and a place in
// DocumentCommand.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Fields.h"
#include "twintailor/sim/Vec3.h"

namespace twintailor::session {

// ---- How a change may change the document ----------------------------------------

enum class Step {
    New,       // starts an undo step
    Continue,  // part of the step before it, as the later moments of one drag
};

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
};

struct ChangeRules {
    Step step = Step::New;  // for Editor::change; a command's step follows from how it is run
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

// A parameter's value, or a field of an operation, set to a number or a formula.
struct SetField {
    std::variant<Param, OperationField> field;
    Expr value;

    using Done = Nothing;
    ChangeRules rules() const { return {}; }
    std::expected<Done, std::string> apply(Document& doc, const Result& before) const;
};

// Corners of a panel moved by as much, on the table. On a symmetric panel a
// mirrored corner moves its original, the mirrored way. A corner that a
// formula places stays; if all of them do, the command says why. One corner
// keeps being moved by the panel's last operation if that moves it by plain
// numbers; otherwise each corner gets an adjustment of its own.
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

// Every command that changes the document.
using DocumentCommand = std::variant<SetField, MoveCorners, DuplicatePanels, DeletePanels, DeleteCorners>;

template <class C>
concept DocumentChange = isAlternative<C, DocumentCommand>;

// ---- Commands on the cloth -------------------------------------------------------

// A particle of the cloth shown, taken hold of and led to `to`. Only a Session
// runs it, and only as a drag: it changes no document, and the whole drag is
// one undo step, which brings back the drape as it was before.
//
//   session.drag(PullCloth{panel, point, to});  // takes hold
//   session.drag(PullCloth{panel, point, further});
//   session.endDrag();                          // lets go; or cancelDrag()
struct PullCloth {
    PanelId panel;
    std::uint32_t point;  // indexes the panel's particles in Session::cloth().mesh
    sim::Vec3 to;
};

} // namespace twintailor::session
