// Unit tests of Editor.cpp: changes, refusals, undo and redo.

#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/session/Editor.h"

using namespace clothcad;
using namespace clothcad::session;

namespace {

struct OnePanel {
    Document doc;
    Param waist;
    PanelId panel;
    OpRef<Rect> rect;
    OnePanel() {
        waist = doc.add(Parameter{.name = "waist", .value = 80});
        panel = doc.add(Panel{.name = "band"});
        rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});
    }
};

double width(const Editor& editor, PanelId panel, OpRef<Rect> rect) { return editor.shapes()[panel].length(rect.bottom()); }

} // namespace

TEST_CASE("a change is made and the outlines follow it")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto versionBefore = editor.version(), shapesBefore = editor.shapesVersion();
    const auto done = editor.change([&](Document& doc) { doc.set(start.waist, Expr(90)); });
    EXPECT(done.has_value(), "the change is made");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "the band follows the waist");
    EXPECT((editor.version() > versionBefore && editor.shapesVersion() > shapesBefore), "both versions go up");
}

TEST_CASE("a change that would break something is refused and says why")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto done = editor.change([&](Document& doc) { doc.set(start.waist, Expr(-5)); });
    EXPECT(!done.has_value(), "a negative waist is refused");
    EXPECT((!done && done.error().find("band") != std::string::npos), "the reason names the panel it would break");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 80.0, "and nothing changed");
    EXPECT(!editor.canUndo(), "a refused change leaves nothing to undo");
}

TEST_CASE("a change that is allowed to break something is made")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto done = editor.change([&](Document& doc) { doc.set(start.waist, Expr(-5)); }, {.ifItBreaks = IfItBreaks::Allow});
    EXPECT(done.has_value(), "it is made");
    EXPECT(!editor.shapes().ok(), "and the problem shows in the outlines");
}

TEST_CASE("a verb that throws is refused with its words")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto done = editor.change([&](Document& doc) { doc.remove(PanelId{999}); });
    EXPECT(!done.has_value(), "the change is refused");
    EXPECT(editor.document().panels().size() == 1u, "and the document is as it was");
}

TEST_CASE("one drag is one undo step")
{
    OnePanel start;
    Editor editor(start.doc);
    for (int value = 81; value <= 90; ++value)
        REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(value)); }, {.step = value == 81 ? Step::New : Step::Continue}));
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "the drag ends at 90");
    EXPECT(editor.undo(), "it can be undone");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 80.0, "one undo takes the whole drag back");
    EXPECT(!editor.canUndo(), "and there is nothing before it");
}

TEST_CASE("undo and redo walk the history both ways")
{
    OnePanel start;
    Editor editor(start.doc);
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(90)); }));
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(100)); }));
    editor.undo();
    editor.undo();
    EXPECT_NEAR(width(editor, start.panel, start.rect), 80.0, "two undos go back to the start");
    EXPECT(editor.redo(), "a redo");
    EXPECT_NEAR(width(editor, start.panel, start.rect), 90.0, "brings the first change back");
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(70)); }));
    EXPECT(!editor.canRedo(), "a new change ends what could be redone");
}

TEST_CASE("moving a panel changes no outline, so nothing is evaluated again")
{
    OnePanel start;
    Editor editor(start.doc);
    const auto shapesBefore = editor.shapesVersion();
    REQUIRE(editor.change([&](Document& doc) { doc.place(start.panel, 30, 40); }, {.touches = Touches::PlacementOnly}));
    EXPECT(editor.shapesVersion() == shapesBefore, "the outlines are left as they were");
    EXPECT(editor.document().get(start.panel).x == 30.0, "the panel has moved");
    EXPECT(editor.undo(), "and it can be undone");
    EXPECT(editor.document().get(start.panel).x == 0.0, "back where it was");
}

TEST_CASE("another document starts a new history")
{
    OnePanel start;
    Editor editor(start.doc);
    REQUIRE(editor.change([&](Document& doc) { doc.set(start.waist, Expr(90)); }));
    editor.replace(Document{});
    EXPECT((editor.document().panels().empty() && !editor.canUndo() && !editor.canRedo()), "nothing to undo or redo");
}
