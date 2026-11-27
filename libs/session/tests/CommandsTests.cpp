// Unit tests of Commands.cpp: what each command does to the work, run as a
// window, a script or a test would run it.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/session/Commands.h"
#include "twintailor/session/Editor.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

// A front 20 by 40 cm, its width a parameter.
struct Front {
    Document doc;
    Param width;
    PanelId panel;
    OpRef<Rect> rect;
    Front() {
        width = doc.add(Parameter{.name = "width", .value = 20_cm});
        panel = doc.add(Panel{.name = "front"});
        rect = doc.add(panel, Rect{.width = width, .height = 40_cm});
    }
};

// A panel of plain numbers, whose corners can be dragged.
struct Plain {
    Document doc;
    PanelId panel;
    OpRef<Rect> rect;
    Plain() {
        panel = doc.add(Panel{.name = "plain"});
        rect = doc.add(panel, Rect{.width = 20_cm, .height = 40_cm});
    }
};

double heightOf(const Editor& editor, PanelId panel, const CornerRef& corner) { return editor.shapes()[panel].position(corner).y; }

} // namespace

TEST_CASE("a parameter or a layer's field is set by one command, one undo step")
{
    Front start;
    Editor editor(start.doc);
    REQUIRE(editor.run(SetField{start.width, 30_cm}));
    EXPECT((editor.shapes()[start.panel].length(start.rect.bottom()) == 30.0), "the outlines follow the parameter");
    REQUIRE(editor.run(SetField{OperationField{start.rect, FieldId{1}}, 50_cm}));
    EXPECT((editor.shapes()[start.panel].length(start.rect.left()) == 50.0), "a field of a layer is set the same way");
    EXPECT((editor.undo() && editor.shapes()[start.panel].length(start.rect.left()) == 40.0), "each command is one undo step");
    EXPECT((editor.undo() && !editor.canUndo()), "and there are no others");
}

TEST_CASE("a command that cannot be done says why and changes nothing")
{
    Front start;
    Editor editor(start.doc);
    const std::uint64_t version = editor.version();
    const auto narrow = editor.run(SetField{start.width, Expr(-1)});
    EXPECT((!narrow && !narrow.error().empty()), "a width below nothing is refused, in words");
    const auto gone = editor.run(DeletePanels{{start.panel, PanelId{999}}});
    EXPECT((!gone && editor.document().contains(start.panel)), "a panel that is not there refuses the whole command: the others stay");
    EXPECT((editor.version() == version && !editor.canUndo() && !editor.unsaved()), "nothing changed, and there is nothing to undo");
}

TEST_CASE("a drag of corners is run from where it began: it says how far in all, and is one undo step")
{
    Plain start;
    Editor editor(start.doc);
    MoveCorners move{.panel = start.panel, .corners = {start.rect.topRight()}, .by = {0, 2}};
    REQUIRE(editor.drag(move));
    move.by = {0, 5};
    REQUIRE(editor.drag(move));
    EXPECT((heightOf(editor, start.panel, start.rect.topRight()) == 45.0), "5 cm in all, not 2 and then 5");
    EXPECT((editor.document().operations(start.panel).size() == 2), "with one adjustment");
    editor.endDrag();

    // The next drag keeps moving the same adjustment.
    move.by = {0, 2};
    REQUIRE(editor.drag(move));
    editor.endDrag();
    EXPECT((heightOf(editor, start.panel, start.rect.topRight()) == 47.0 && editor.document().operations(start.panel).size() == 2), "and so does the next");
    EXPECT((editor.undo() && heightOf(editor, start.panel, start.rect.topRight()) == 45.0), "one undo for the second drag");
    EXPECT((editor.undo() && editor.document().operations(start.panel).size() == 1 && !editor.canUndo()), "and one for the first");
}

TEST_CASE("a drag that is cancelled leaves nothing: neither a change nor something to redo")
{
    Plain start;
    Editor editor(start.doc);
    REQUIRE(editor.drag(MoveCorners{.panel = start.panel, .corners = {start.rect.topRight()}, .by = {3, 0}}));
    REQUIRE(editor.drag(MoveCorners{.panel = start.panel, .corners = {start.rect.topRight()}, .by = {6, 0}}));
    EXPECT((editor.cancelDrag()), "it is taken back");
    EXPECT((editor.document().operations(start.panel).size() == 1 && !editor.canUndo() && !editor.canRedo() && !editor.unsaved()), "as if it never was");
    EXPECT((!editor.cancelDrag()), "there is no drag any more");
}

TEST_CASE("a moment of a drag that cannot be done leaves the one before")
{
    Front start;
    Editor editor(start.doc);
    REQUIRE(editor.drag(SetField{start.width, 30_cm}));
    const auto refused = editor.drag(SetField{start.width, Expr(-5)});
    EXPECT((!refused && editor.shapes()[start.panel].length(start.rect.bottom()) == 30.0), "the width stays as the moment before had it");
    editor.endDrag();
    EXPECT((editor.undo() && editor.shapes()[start.panel].length(start.rect.bottom()) == 20.0 && !editor.canUndo()), "one undo step");
}

TEST_CASE("a corner that a formula places is not moved, and the command says why")
{
    Front start;
    Editor editor(start.doc);
    // The bottom right corner is where the width parameter puts it.
    const auto locked = editor.run(MoveCorners{.panel = start.panel, .corners = {start.rect.bottomRight()}, .by = {1, 0}});
    EXPECT((!locked && locked.error().find("formula") != std::string::npos && !editor.canUndo()), "it says that a formula sets it");
}

TEST_CASE("a mirrored instance is a command that hands back the new panels and how they were sewn")
{
    Front start;
    const PanelId back = start.doc.add(Panel{.name = "back", .x = 40});
    const OpRef<Rect> backRect = start.doc.add(back, Rect{.width = start.width, .height = 40_cm});
    start.doc.add(Seam{.a = {start.panel, start.rect.right()}, .b = {back, backRect.left()}});
    Editor editor(start.doc);
    const auto made = editor.run(DuplicatePanels{.panels = {start.panel, back}, .how = Follow::MirroredInstance});
    REQUIRE((made.has_value() && made->panels.size() == 2));
    const Document& doc = editor.document();
    EXPECT((doc.get(made->panels[0]).name == "front mirrored" && doc.instanceOf(made->panels[0])->mirrored), "named after its source, mirrored");
    EXPECT((made->seams == 1 && doc.seams().size() == 2), "sewn as the sources are");
    EXPECT((editor.undo() && editor.document().panels().size() == 2 && editor.document().seams().size() == 1 && !editor.canUndo()), "all in one undo step");
}

TEST_CASE("panels are deleted by one command; what used them shows a problem, and undo brings all back")
{
    Front start;
    const PanelId copy = start.doc.add(Panel{.name = "copy"});
    start.doc.add(copy, Instance{.source = start.panel});
    Editor editor(start.doc);
    REQUIRE(editor.run(DeletePanels{{start.panel}}));
    EXPECT((!editor.document().contains(start.panel) && !editor.shapes().errors.empty()), "allowed, though the instance has lost its source");
    EXPECT((editor.undo() && editor.document().contains(start.panel) && editor.shapes().errors.empty()), "undo brings it back");
}

TEST_CASE("only a corner added as a point is deleted; a shape's corner says which layer to change")
{
    Front start;
    const OpRef<AddPoint> point = start.doc.add(start.panel, AddPoint{.at = {start.rect.top(), Expr(0.5)}});
    Editor editor(start.doc);
    const auto shapes = editor.run(DeleteCorners{.panel = start.panel, .corners = {start.rect.topLeft()}});
    EXPECT((!shapes && shapes.error().find("1. Rectangle") != std::string::npos && !editor.canUndo()), "a rectangle's corner stays");
    REQUIRE(editor.run(DeleteCorners{.panel = start.panel, .corners = {point.corner()}}));
    EXPECT((editor.document().operations(start.panel).size() == 1), "the point's layer is gone");
}
