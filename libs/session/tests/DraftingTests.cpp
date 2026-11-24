// Unit tests of Drafting.cpp: what a gesture on the table changes, shared by
// every window. The gestures are given as a window reports them.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/session/Drafting.h"
#include "clothcad/session/Locks.h"

using namespace clothcad;
using namespace clothcad::session;

namespace {

template <class T> std::size_t count(const Document& doc, PanelId panel) {
    std::size_t n = 0;
    for (OpId op : doc.operations(panel)) n += std::holds_alternative<T>(doc.get(op));
    return n;
}

template <class T> const T& last(const Document& doc, PanelId panel) {
    const T* found = nullptr;
    for (OpId op : doc.operations(panel))
        if (const T* t = std::get_if<T>(&doc.get(op))) found = t;
    REQUIRE(found != nullptr);
    return *found;
}

// A drag as a window reports it: begun, moved once, ended.
void drag(Drafting& drafting, Editor& editor, auto gesture, double dx, double dy) {
    gesture.dx = 0, gesture.dy = 0, gesture.phase = DragPhase::Begin;
    drafting.handle(editor, gesture);
    gesture.dx = dx, gesture.dy = dy, gesture.phase = DragPhase::Continue;
    drafting.handle(editor, gesture);
    gesture.phase = DragPhase::End;
    drafting.handle(editor, gesture);
}

// Half of a symmetric panel: a rectangle 10 by 20 cm, mirrored across its left edge.
struct Half {
    Document doc;
    PanelId panel;
    OpRef<Rect> rect;
    OpRef<Mirror> fold;
    Half() {
        panel = doc.add(Panel{.name = "half"});
        rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
        fold = doc.add(panel, Mirror{.across = rect.left()});
    }
};

} // namespace

TEST_CASE("on a symmetric panel a mirrored corner moves its original, the mirrored way")
{
    Half half;
    Editor editor(half.doc);
    Drafting drafting;
    drag(drafting, editor, MoveCorner{half.panel, half.fold.image(half.rect.bottomRight()), 0, 0, DragPhase::Begin}, -4.0, 0.0);
    const Document& doc = editor.document();
    EXPECT((count<AdjustPoint>(doc, half.panel) == 1 && last<AdjustPoint>(doc, half.panel).corner == half.rect.bottomRight()), "the drawn corner is adjusted");
    const Pattern& shape = editor.shapes()[half.panel];
    const double drawn = shape.position(half.rect.bottomRight()).x, image = shape.position(half.fold.image(half.rect.bottomRight())).x;
    EXPECT((std::abs(drawn - 14.0) < 1e-9 && std::abs(drawn + image) < 1e-9), "dragged 4 cm out on the mirrored side, both sides grow 4 cm: it stays symmetric");
    EXPECT((editor.undo() && count<AdjustPoint>(editor.document(), half.panel) == 0), "the drag is one undo step");
}

TEST_CASE("a corner on the mirror's axis only slides along it")
{
    Half half;
    Editor editor(half.doc);
    Drafting drafting;
    drag(drafting, editor, MoveCorner{half.panel, half.rect.topLeft(), 0, 0, DragPhase::Begin}, 5.0, 3.0);
    const Vec2 top = editor.shapes()[half.panel].position(half.rect.topLeft());
    EXPECT((editor.shapes().ok() && std::abs(top.x) < 1e-9 && std::abs(top.y - 23.0) < 1e-9), "it goes up with the mouse, and not off the axis");
    EXPECT((editor.shapes()[half.panel].axes.size() == 1), "the mirror still holds");
}

TEST_CASE("bending a mirrored edge bends its original; a line may not cross the mirror")
{
    Half half;
    Editor editor(half.doc);
    Drafting drafting;
    const Outcome bent = drafting.handle(editor, BendEdge{half.panel, half.fold.image(half.rect.right())});
    EXPECT((bent.toolDone && count<Bezier>(editor.document(), half.panel) == 1 && last<Bezier>(editor.document(), half.panel).edge == half.rect.right()), "the original edge is made a curve");
    EXPECT((editor.shapes()[half.panel].edge(half.fold.image(half.rect.right())).cubic.has_value()), "so both sides are curves");

    const Outcome across = drafting.handle(editor, DrawLine{half.panel, {half.rect.bottom(), 0.5, {}}, {half.fold.image(half.rect.bottom()), 0.5, {}}});
    EXPECT((across.refused && !across.notice.empty() && count<LineBetween>(editor.document(), half.panel) == 0), "a line from one half to the other is refused, with why");
}

TEST_CASE("bending an arc follows it; a whole circle has no ends to bend between")
{
    Document doc;
    const PanelId slice = doc.add(Panel{.name = "slice"});
    const OpRef<Circle> quarter = doc.add(slice, Circle{.radius = 20_cm, .sweep = 90_deg});
    const PanelId whole = doc.add(Panel{.name = "whole"});
    const OpRef<Circle> circle = doc.add(whole, Circle{.radius = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    const double arcBefore = editor.shapes()[slice].length(quarter.arc());
    drafting.handle(editor, BendEdge{slice, quarter.arc()});
    const Pattern& bent = editor.shapes()[slice];
    EXPECT((count<Bezier>(editor.document(), slice) == 1 && bent.edge(quarter.arc()).cubic.has_value()), "an arc becomes a free curve");
    EXPECT((std::abs(bent.length(quarter.arc()) - arcBefore) < 0.02 * arcBefore), "that follows the arc it replaced");

    const Outcome refused = drafting.handle(editor, BendEdge{whole, circle.arc()});
    EXPECT((!refused.notice.empty() && count<Bezier>(editor.document(), whole) == 0), "a whole circle is left as it is, and says why");
    drag(drafting, editor, MoveCorner{whole, circle.start(), 0, 0, DragPhase::Begin}, 3.0, 0.0);
    EXPECT((count<AdjustPoint>(editor.document(), whole) == 0 && editor.shapes().ok()), "its only point stays put");
}

TEST_CASE("a panel turns about the middle of its box; a corner of a turned panel follows the mouse")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box", .x = 10, .y = 10});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, RotatePanel{box, 0.0, DragPhase::Begin});
    drafting.handle(editor, RotatePanel{box, -90.0, DragPhase::End});
    const Panel& turned = editor.document().get(box);
    const Vec2 middle = clothcad::turned(Vec2{10.0, 5.0}, turned.angle);
    EXPECT((std::abs(turned.angle + 90.0) < 1e-9), "it turns by the angle given");
    EXPECT((std::abs(turned.x + middle.x - 20.0) < 1e-9 && std::abs(turned.y + middle.y - 15.0) < 1e-9), "about the middle of its box, which stays put");
    EXPECT((editor.document().operations(box).size() == 1), "turning only places it");

    // Turned a quarter clockwise, the panel's own +x points down the table.
    const Vec2 before = editor.shapes()[box].position(rect.topRight());
    drag(drafting, editor, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin}, 0.0, -3.0);
    const Vec2 after = editor.shapes()[box].position(rect.topRight());
    EXPECT((std::abs(after.x - before.x - 3.0) < 1e-9 && std::abs(after.y - before.y) < 1e-9), "dragged down the table, the corner moves along the panel's own x");
}

TEST_CASE("selected corners move together, as one undo step")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, SelectCorners{box, {rect.topRight()}, false});
    drafting.handle(editor, SelectCorners{box, {rect.bottomRight()}, true});
    EXPECT((drafting.selection.corners.size() == 2 && drafting.selection.panel == box), "a second corner is added");
    drag(drafting, editor, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin}, 5.0, 0.0);
    EXPECT((count<AdjustPoint>(editor.document(), box) == 2 && std::abs(editor.shapes()[box].length(rect.bottom()) - 25.0) < 1e-9), "dragging one moves both");
    EXPECT((editor.undo() && count<AdjustPoint>(editor.document(), box) == 0), "and one undo takes them both back");
    drafting.handle(editor, SelectCorners{box, {rect.topRight()}, true});
    EXPECT((drafting.selection.corners.size() == 1), "clicking a selected corner with Shift takes it out");
}

TEST_CASE("two edges are sewn once; a locked corner and a length that is no setting say why")
{
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 20_cm});
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = width, .height = 10_cm});
    const PanelId back = doc.add(Panel{.name = "back", .x = 30});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, SewEdges{{front, frontRect.right()}, {back, backRect.left()}});
    EXPECT((editor.document().seams().size() == 1 && editor.shapes().seams.front().sewn()), "two edges are sewn");
    const Outcome again = drafting.handle(editor, SewEdges{{back, backRect.left()}, {front, frontRect.right()}});
    EXPECT((editor.document().seams().size() == 1 && !again.notice.empty()), "the same two, either way round, are not sewn twice");

    const Outcome locked = drafting.handle(editor, MoveCorner{front, frontRect.bottomRight(), 0, 0, DragPhase::Begin});
    EXPECT((count<AdjustPoint>(editor.document(), front) == 0 && locked.notice.find("formula") != std::string::npos), "a corner a formula places is locked, and says so");

    const Outcome typed = drafting.handle(editor, EditLength{back, backRect.bottom()});
    EXPECT((typed.typeLength && typed.typeLength->operation == OpId(backRect)), "a rectangle's width is a setting to type");
    drag(drafting, editor, MoveCorner{back, backRect.bottomRight(), 0, 0, DragPhase::Begin}, 2.0, 0.0);
    const Outcome moved = drafting.handle(editor, EditLength{back, backRect.bottom()});
    EXPECT((!moved.typeLength && !moved.notice.empty()), "once a corner was dragged the length follows the corners, and it says so");
}

TEST_CASE("the scale gizmo adds a Scale layer, then keeps changing it, one undo step a drag")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box", .x = 10, .y = 10});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    const auto scaleBy = [&](double x, double y) {
        drafting.handle(editor, ScalePanel{box, 1.0, 1.0, DragPhase::Begin});
        drafting.handle(editor, ScalePanel{box, x, y, DragPhase::Continue});
        return drafting.handle(editor, ScalePanel{box, x, y, DragPhase::End});
    };
    scaleBy(1.5, 1.0);
    EXPECT((count<Scale>(editor.document(), box) == 1), "the first drag adds a Scale at the end");
    EXPECT_NEAR((editor.shapes()[box].length(rect.bottom())), (30.0), "half as wide again");
    EXPECT_NEAR((editor.shapes()[box].position(rect.bottomLeft()).x), (-5.0), "about the middle of its box");
    scaleBy(1.0, 2.0);
    EXPECT((count<Scale>(editor.document(), box) == 1), "a second drag changes the same Scale");
    EXPECT((asNumber(last<Scale>(editor.document(), box).x) == 1.5 && asNumber(last<Scale>(editor.document(), box).y) == 2.0), "on top of what it had");
    EXPECT((editor.undo() && asNumber(last<Scale>(editor.document(), box).y) == 1.0), "one undo takes back the second drag");
    EXPECT((editor.undo() && count<Scale>(editor.document(), box) == 0), "and one more the first, with its layer");
    EXPECT((editor.document().get(box).x == 10.0), "scaling is shape, not placement: the panel's place stays");

    // A Scale that a formula drives is not dragged.
    Document driven = doc;
    const Param wide = driven.add(Parameter{.name = "wide", .value = 1.2, .unit = Unit::Fraction});
    driven.add(box, Scale{.x = wide, .y = 1.0});
    Editor locked(driven);
    const Outcome refused = drafting.handle(locked, ScalePanel{box, 2.0, 2.0, DragPhase::Begin});
    EXPECT((refused.refused && refused.notice.find("formula") != std::string::npos), "it says a formula sets it");

    // A change that would break the outline is refused, and says why.
    Document round;
    const PanelId circle = round.add(Panel{.name = "circle"});
    round.add(circle, Circle{.radius = 10_cm});
    Editor circleEditor(round);
    drafting.handle(circleEditor, ScalePanel{circle, 1.0, 1.0, DragPhase::Begin});
    const Outcome oval = drafting.handle(circleEditor, ScalePanel{circle, 1.5, 1.0, DragPhase::Continue});
    EXPECT((oval.refused && oval.notice.find("evenly") != std::string::npos), "a whole circle is not stretched one way only");
    EXPECT((count<Scale>(circleEditor.document(), circle) == 0), "a refused first change leaves no empty Scale layer behind");
    drafting.handle(circleEditor, ScalePanel{circle, 1.5, 1.5, DragPhase::End});
    EXPECT((asNumber(last<Scale>(circleEditor.document(), circle).x) == 1.5), "but it is scaled evenly");
}

TEST_CASE("a switched-off Scale is not the one the gizmo changes")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    const OpRef<Scale> off = doc.add(box, Scale{.x = 1.5, .y = 1.5});
    doc.setEnabled(off.id, false);
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, ScalePanel{box, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(editor, ScalePanel{box, 2.0, 2.0, DragPhase::End});
    EXPECT((count<Scale>(editor.document(), box) == 2), "a new Scale is added instead");
    EXPECT((asNumber(editor.document().get(off).x) == 1.5), "the switched-off one is left as it was");
}

TEST_CASE("a refused scale change does not take the next accepted one into an earlier undo step")
{
    Document doc;
    const PanelId circle = doc.add(Panel{.name = "circle"});
    doc.add(circle, Circle{.radius = 10_cm});
    const OpRef<Scale> layer = doc.add(circle, Scale{.x = 1.2, .y = 1.2});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, DrawRect{50.0, 50.0, 5.0, 5.0});  // an earlier step, which must stay
    drafting.handle(editor, ScalePanel{circle, 1.0, 1.0, DragPhase::Begin});
    EXPECT((drafting.handle(editor, ScalePanel{circle, 1.5, 1.0, DragPhase::Continue}).refused), "stretching one way is refused");
    drafting.handle(editor, ScalePanel{circle, 1.5, 1.5, DragPhase::End});
    EXPECT_NEAR((*asNumber(editor.document().get(layer).x)), (1.8), "then the even change is accepted");
    EXPECT((editor.undo() && editor.document().panels().size() == 2 && asNumber(editor.document().get(layer).x) == 1.2), "one undo takes back only the drag");
}

TEST_CASE("a handle dragged under a later Scale follows the pointer")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, BendEdge{box, rect.bottom()});
    const OpRef<Bezier> curve = *curveOf(editor.document(), box, rect.bottom());
    const double before = *asNumber(editor.document().get(curve).startHandle.x);
    EXPECT((editor.change([&](Document& d) { d.add(box, Scale{.x = 2.0, .y = 1.0}); }).has_value()), "a Scale after the curve");
    drag(drafting, editor, MoveHandle{box, rect.bottom(), false, 0, 0, DragPhase::Begin}, 4.0, 0.0);
    EXPECT_NEAR((*asNumber(editor.document().get(curve).startHandle.x) - before), (2.0), "4 cm of drag moves a handle that is drawn twice as wide by 2 cm of offset");
}

TEST_CASE("a symmetric panel is scaled before its mirror, and stays symmetric")
{
    Half half;
    Editor editor(half.doc);
    Drafting drafting;
    drafting.handle(editor, ScalePanel{half.panel, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(editor, ScalePanel{half.panel, 2.0, 1.0, DragPhase::End});
    const Pattern& shape = editor.shapes()[half.panel];
    EXPECT((editor.shapes().ok()), "nothing breaks");
    EXPECT_NEAR((shape.length(half.rect.bottom()) + shape.length(half.fold.image(half.rect.bottom()))), (40.0), "both halves are twice as wide");
    EXPECT_NEAR((shape.length(half.rect.bottom())), (shape.length(half.fold.image(half.rect.bottom()))), "and the same width");
    drafting.handle(editor, ScalePanel{half.panel, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(editor, ScalePanel{half.panel, 1.0, 0.5, DragPhase::End});
    EXPECT((count<Scale>(editor.document(), half.panel) == 1), "the mirror applied last is looked past: the same Scale changes");
}

TEST_CASE("a panel carried and turned in space is placed, not reshaped, one undo step a drag")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    doc.arrange(box, Arrangement{.placed = true, .x = 5, .y = 100, .z = 20, .turn = 30});
    Editor editor(doc);
    Drafting drafting;
    const Arrangement was = editor.document().get(box).arrangement;
    const std::uint64_t shapes = editor.shapesVersion();

    drafting.handle(editor, MoveInSpace{box, {0, 0, 0}, DragPhase::Begin});
    drafting.handle(editor, MoveInSpace{box, {3, -2, 1}, DragPhase::Continue});
    drafting.handle(editor, MoveInSpace{box, {4, -2, 1}, DragPhase::End});
    const Arrangement moved = editor.document().get(box).arrangement;
    EXPECT((moved.x == 9.0 && moved.y == 98.0 && moved.z == 21.0 && moved.turn == 30.0), "carried by how far it was dragged since the drag began");
    EXPECT((editor.shapesVersion() == shapes && std::abs(editor.shapes()[box].length(rect.bottom()) - 20.0) < 1e-9), "placing is not shape");
    EXPECT((editor.undo() && editor.document().get(box).arrangement == was), "the whole drag is one undo step");

    drafting.handle(editor, TurnInSpace{box, {0, 1, 0}, 0.0, DragPhase::Begin});
    drafting.handle(editor, TurnInSpace{box, {0, 1, 0}, 20.0, DragPhase::Continue});
    drafting.handle(editor, TurnInSpace{box, {0, 1, 0}, 15.0, DragPhase::End});
    const Arrangement turned = editor.document().get(box).arrangement;
    EXPECT((std::abs(turned.turn - 45.0) < 1e-9 && turned.x == was.x && turned.z == was.z), "turned about y by the angle since the drag began, about its anchor");
    EXPECT((editor.undo() && editor.document().get(box).arrangement == was), "one undo step too");
    EXPECT((!editor.canUndo()), "and nothing else was changed");
}

TEST_CASE("Drafting keeps the tool in use; a drawing tool that has done its work goes back to Select")
{
    Document doc;
    Editor editor(doc);
    Drafting drafting;
    EXPECT((drafting.tool == Tool::Select), "it starts with Select");
    EXPECT((!gizmoOf(Tool::Select) && gizmoOf(Tool::Move) == GizmoMode::Move && gizmoOf(Tool::Rotate) == GizmoMode::Rotate &&
            gizmoOf(Tool::Scale) == GizmoMode::Scale && !gizmoOf(Tool::Rectangle)),
           "W, E and R show their gizmo; Select and the drawing tools none");
    drafting.tool = Tool::Rectangle;
    drafting.handle(editor, DrawRect{0, 0, 20, 10});
    EXPECT((drafting.tool == Tool::Select && drafting.selection.panel.has_value()), "a rectangle drawn, the new panel is selected with Select");
    drafting.tool = Tool::Rotate;
    drafting.handle(editor, SelectPanel{std::nullopt});
    EXPECT((drafting.tool == Tool::Rotate), "selecting keeps the tool");
}

TEST_CASE("a panel is deleted with its layers, one undo step; what used it shows a problem")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    const PanelId twin = doc.add(Panel{.name = "twin"});
    doc.add(twin, Instance{.source = front});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanel{front});

    const Outcome done = drafting.handle(editor, DeletePanel{front});
    EXPECT((done.notice.empty() && !editor.document().contains(front)), "the panel is gone");
    EXPECT((!drafting.selection.panel.has_value()), "and nothing is selected any more");
    EXPECT((!editor.shapes().ok()), "the instance that followed it shows a problem; the delete was not refused");
    EXPECT((editor.undo() && editor.document().contains(front) && editor.shapes().ok()), "one undo brings it back, and the instance is well again");
    EXPECT((!editor.canUndo()), "that was one step");

    drafting.handle(editor, DeletePanel{PanelId{999}});
    EXPECT((!editor.canUndo()), "a panel that does not exist is nothing to delete");
}

TEST_CASE("Delete with no corner selected deletes the selected panel; with nothing selected it says so")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;

    const Outcome nothing = drafting.handle(editor, DeleteSelection{});
    EXPECT((nothing.refused && !nothing.notice.empty() && editor.document().contains(front)), "nothing selected: a sentence and no change");

    drafting.handle(editor, SelectPanel{front});
    drafting.handle(editor, DeleteSelection{});
    EXPECT((!editor.document().contains(front) && !drafting.selection.panel), "the selected panel goes");
    EXPECT((editor.undo() && editor.document().contains(front) && !editor.canUndo()), "as one undo step");
}

TEST_CASE("Delete removes corners added with the Point tool and refuses the corners of a shape")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> rect = doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, AddPointOn{front, OutlinePoint{.edge = rect.bottom(), .along = 0.25}});
    drafting.handle(editor, AddPointOn{front, OutlinePoint{.edge = rect.top(), .along = 0.5}});
    REQUIRE(editor.document().operations(front).size() == 3);
    const std::vector<OpId> layers = editor.document().operations(front);
    const CornerRef first = OpRef<AddPoint>(layers[1]).corner(), second = OpRef<AddPoint>(layers[2]).corner();

    drafting.handle(editor, SelectCorners{front, {first}, false});
    const Outcome done = drafting.handle(editor, DeleteSelection{});
    EXPECT((!done.refused && editor.document().operations(front).size() == 2 && editor.document().operations(front)[1] == layers[2]), "that point's layer is dropped");
    EXPECT((drafting.selection.corners.empty() && drafting.selection.panel == front), "no corner is selected now, the panel still is");
    EXPECT((editor.undo() && editor.document().operations(front).size() == 3), "undo brings the point back");

    drafting.handle(editor, SelectCorners{front, {first, second}, false});
    drafting.handle(editor, DeleteSelection{});
    EXPECT((editor.document().operations(front).size() == 1), "two points go together");
    EXPECT((editor.undo() && editor.document().operations(front).size() == 3), "in one undo step");

    drafting.handle(editor, SelectCorners{front, {rect.topLeft()}, false});
    const std::size_t steps = editor.version();
    const Outcome refused = drafting.handle(editor, DeleteSelection{});
    EXPECT((refused.refused && refused.notice.find("1. Rectangle") != std::string::npos && refused.notice.find("Remove or change that layer") != std::string::npos),
           "a corner of the rectangle says which layer it belongs to");
    EXPECT((editor.version() == steps && drafting.selection.corners.size() == 1), "and nothing changes");

    drafting.handle(editor, SelectCorners{front, {first, rect.topLeft()}, false});
    const Outcome mixed = drafting.handle(editor, DeleteSelection{});
    EXPECT((mixed.refused && editor.document().operations(front).size() == 3), "one corner of a shape among the points refuses all of them");
}

TEST_CASE("a copied panel is pasted with its middle at the pointer and follows the same parameter")
{
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 20_cm});
    const PanelId front = doc.add(Panel{.name = "front", .x = 5, .y = 5});
    const OpRef<Rect> rect = doc.add(front, Rect{.width = width, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    EXPECT((!drafting.copied().has_value()), "nothing is copied at first");

    const Outcome nothing = drafting.handle(editor, PastePanel{Vec2{0, 0}});
    EXPECT((nothing.refused && !nothing.notice.empty() && !editor.canUndo()), "pasting before copying says so");

    const Outcome copied = drafting.handle(editor, CopyPanel{front});
    EXPECT((!copied.refused && !copied.notice.empty() && drafting.copied() == front && !editor.canUndo()), "copying remembers the panel and changes no document");

    drafting.handle(editor, PastePanel{Vec2{100, 40}});
    const std::vector<PanelId> panels = editor.document().panels();
    REQUIRE(panels.size() == 2);
    const PanelId pasted = panels[1];
    EXPECT((drafting.selection.panel == pasted && editor.document().get(pasted).name == "front copy"), "the copy is selected and named for its source");
    const Panel& placed = editor.document().get(pasted);
    EXPECT_NEAR(placed.x + 10.0, 100.0, "the middle of its 20 cm width is under the pointer in x");
    EXPECT_NEAR(placed.y + 5.0, 40.0, "and its 10 cm height in y");
    EXPECT((editor.undo() && editor.document().panels().size() == 1 && !editor.canUndo()), "a paste is one undo step");
    editor.redo();

    drafting.handle(editor, PastePanel{Vec2{100, 80}});
    EXPECT((editor.document().panels().size() == 3 && editor.document().get(editor.document().panels()[2]).name == "front copy 2"), "pasting again copies the same panel, with a free name");

    EXPECT(editor.change([&](Document& d) { d.set(width, Expr(30_cm)); }).has_value(), "the parameter is changed");
    EXPECT_NEAR(editor.shapes()[front].length(rect.bottom()), 30.0, "the source follows the parameter");
    EXPECT_NEAR(editor.shapes()[pasted].length(OpRef<Rect>(editor.document().operations(pasted)[0]).bottom()), 30.0, "and so does the copy");
}

TEST_CASE("pasting without a place goes beside the original; a copied panel that is gone is refused")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, CopyPanel{front});
    drafting.handle(editor, PastePanel{std::nullopt});
    const PanelId pasted = editor.document().panels()[1];
    EXPECT((editor.document().get(pasted).x > 20.0 && editor.document().get(pasted).y == 0.0), "just right of the original, as Duplicate does");

    drafting.handle(editor, DeletePanel{front});
    const std::uint64_t version = editor.version();
    const Outcome gone = drafting.handle(editor, PastePanel{Vec2{0, 0}});
    EXPECT((gone.refused && !gone.notice.empty() && editor.version() == version), "the panel is gone: a sentence, and no change");
    EXPECT((!drafting.copied().has_value()), "and nothing stays copied");
}

TEST_CASE("Add beside makes a copy, an instance or a mirrored instance just right of the source, selected")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front", .x = 10, .y = 3});
    doc.add(front, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;

    drafting.handle(editor, AddBeside{front, Follow::Copy});
    const PanelId copy = editor.document().panels()[1];
    EXPECT((editor.document().get(copy).name == "front copy" && drafting.selection.panel == copy), "a copy, named for its source and selected");
    EXPECT((editor.document().get(copy).x >= 10.0 + 20.0 && editor.document().get(copy).y == 3.0), "clear of the source's right edge, on its row");
    EXPECT((count<Rect>(editor.document(), copy) == 1), "with the source's layers");

    drafting.handle(editor, AddBeside{front, Follow::Instance});
    const PanelId instance = editor.document().panels()[2];
    EXPECT((count<Instance>(editor.document(), instance) == 1 && !last<Instance>(editor.document(), instance).mirrored), "an instance follows the source");
    drafting.handle(editor, AddBeside{front, Follow::MirroredInstance});
    const PanelId mirrored = editor.document().panels()[3];
    EXPECT((last<Instance>(editor.document(), mirrored).mirrored), "a mirrored instance is its mirror image");
    EXPECT((editor.shapes().ok()), "all of them evaluate");
    EXPECT((editor.undo() && editor.undo() && editor.undo() && editor.document().panels().size() == 1), "each is one undo step");
}
