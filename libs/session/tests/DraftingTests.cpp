// Unit tests of Drafting.cpp: what a gesture on the table changes, shared by
// every window. The gestures are given as a window reports them.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/session/Drafting.h"

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
