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

TEST_CASE("a corner held to one axis does not move along the other, even on a turned panel")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box", .x = 3, .y = 4, .angle = 30.0});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    // Where a corner is on the table.
    const auto onTable = [&](CornerRef corner) {
        const Vec2 own = turned(editor.shapes()[box].position(corner), editor.document().get(box).angle);
        return Vec2{own.x + editor.document().get(box).x, own.y + editor.document().get(box).y};
    };
    const Vec2 before = onTable(rect.topRight());

    // The pointer wanders in y as well, but only x counts.
    drag(drafting, editor, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin, Along::X}, 5.0, 2.7);
    Vec2 after = onTable(rect.topRight());
    EXPECT((std::abs(after.x - before.x - 5.0) < 1e-9), "it follows the pointer along x");
    EXPECT((std::abs(after.y - before.y) < 1e-9), "and its y on the table does not change by even 1e-9 cm");
    EXPECT((count<AdjustPoint>(editor.document(), box) == 1), "as one layer, in one undo step");
    EXPECT((editor.undo() && count<AdjustPoint>(editor.document(), box) == 0), "which one undo takes back");

    drag(drafting, editor, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin, Along::Y}, 1.9, -4.0);
    after = onTable(rect.topRight());
    EXPECT((std::abs(after.x - before.x) < 1e-9 && std::abs(after.y - before.y + 4.0) < 1e-9), "held to y, only y changes");
    EXPECT((editor.undo()), "and one undo takes it back");

    drag(drafting, editor, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin}, 1.9, -4.0);
    after = onTable(rect.topRight());
    EXPECT((std::abs(after.x - before.x - 1.9) < 1e-9 && std::abs(after.y - before.y + 4.0) < 1e-9), "not held, it goes both ways");
}

TEST_CASE("selected corners held to one axis all keep their other coordinate")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, SelectCorners{box, {rect.topRight(), rect.bottomRight()}, Pick::Replace});
    const Vec2 top = editor.shapes()[box].position(rect.topRight()), bottom = editor.shapes()[box].position(rect.bottomRight());
    drag(drafting, editor, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin, Along::Y}, 3.3, 2.0);
    const Vec2 top2 = editor.shapes()[box].position(rect.topRight()), bottom2 = editor.shapes()[box].position(rect.bottomRight());
    EXPECT((top2.x == top.x && bottom2.x == bottom.x), "neither corner moves sideways");
    EXPECT((std::abs(top2.y - top.y - 2.0) < 1e-9 && std::abs(bottom2.y - bottom.y - 2.0) < 1e-9), "both move up together");
}

TEST_CASE("the axis a drag went furthest along")
{
    EXPECT((axisOf(5.0, 1.0) == Along::X && axisOf(-5.0, 1.0) == Along::X), "sideways, either way, is x");
    EXPECT((axisOf(1.0, -5.0) == Along::Y && axisOf(0.0, 2.0) == Along::Y), "up or down is y");
    EXPECT((axisOf(3.0, 3.0) == Along::X), "a tie is x");
}

TEST_CASE("corners are picked to replace, to add or to toggle what is selected")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    const CornerRef a = rect.topRight(), b = rect.bottomRight(), c = rect.topLeft();
    drafting.handle(editor, SelectCorners{box, {a}, Pick::Replace});
    drafting.handle(editor, SelectCorners{box, {b, c}, Pick::Replace});
    EXPECT((drafting.selection.corners == std::vector<CornerRef>{b, c}), "Replace leaves only what was picked");
    drafting.handle(editor, SelectCorners{box, {b}, Pick::Add});
    EXPECT((drafting.selection.corners.size() == 2), "Add of a corner already selected keeps it: a box over one corner does not take it out");
    drafting.handle(editor, SelectCorners{box, {a, b}, Pick::Add});
    EXPECT((drafting.selection.corners == std::vector<CornerRef>{b, c, a}), "Add joins the new ones");
    drafting.handle(editor, SelectCorners{box, {b, rect.bottomLeft()}, Pick::Toggle});
    EXPECT((drafting.selection.corners == std::vector<CornerRef>{c, a, rect.bottomLeft()}), "Toggle takes out the one selected and puts in the other");
}

TEST_CASE("selected corners move together, as one undo step")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box"});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, SelectCorners{box, {rect.topRight()}, Pick::Replace});
    drafting.handle(editor, SelectCorners{box, {rect.bottomRight()}, Pick::Add});
    EXPECT((drafting.selection.corners.size() == 2 && drafting.selection.panel == box), "a second corner is added");
    drag(drafting, editor, MoveCorner{box, rect.topRight(), 0, 0, DragPhase::Begin}, 5.0, 0.0);
    EXPECT((count<AdjustPoint>(editor.document(), box) == 2 && std::abs(editor.shapes()[box].length(rect.bottom()) - 25.0) < 1e-9), "dragging one moves both");
    EXPECT((editor.undo() && count<AdjustPoint>(editor.document(), box) == 0), "and one undo takes them both back");
    drafting.handle(editor, SelectCorners{box, {rect.topRight()}, Pick::Toggle});
    EXPECT((drafting.selection.corners.size() == 1), "clicking a selected corner with Shift takes it out");
}

TEST_CASE("two edges are sewn once; a locked corner says why, and a length that follows its corners is given a setting")
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
    EXPECT((moved.typeLength && !(moved.typeLength->operation == OpId(backRect)) && count<SetLength>(editor.document(), back) == 1),
           "once a corner was dragged the length follows the corners: the edge is given a length of its own to type");
}

TEST_CASE("the scale gizmo scales a Rect, a Circle or a RingSector itself and adds no layer, one undo step a drag")
{
    Document doc;
    const PanelId box = doc.add(Panel{.name = "box", .x = 10, .y = 10});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = 20_cm, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    const auto scaleBy = [&](PanelId panel, double x, double y) {
        drafting.handle(editor, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
        drafting.handle(editor, ScalePanel{panel, x, y, DragPhase::Continue});
        return drafting.handle(editor, ScalePanel{panel, x, y, DragPhase::End});
    };
    scaleBy(box, 1.5, 1.0);
    EXPECT((count<Scale>(editor.document(), box) == 0), "no Scale layer is added: the rectangle itself changes");
    EXPECT((asNumber(editor.document().get(rect).width) == 30.0 && asNumber(editor.document().get(rect).height) == 10.0), "half as wide again, as tall");
    const auto middleOf = [&]() {
        const Bounds own = *bounds(editor.shapes()[box]);
        return Vec2{editor.document().get(box).x + (own.min.x + own.max.x) / 2.0, editor.document().get(box).y + (own.min.y + own.max.y) / 2.0};
    };
    EXPECT_NEAR((middleOf().x), (20.0), "the middle of the panel's box stays where it was on the table (10 + 10)");
    EXPECT_NEAR((middleOf().y), (15.0), "in y as well");
    scaleBy(box, 1.0, 2.0);
    EXPECT((asNumber(editor.document().get(rect).width) == 30.0 && asNumber(editor.document().get(rect).height) == 20.0), "a second drag goes on from what it had");
    EXPECT((editor.undo() && asNumber(editor.document().get(rect).height) == 10.0), "one undo takes back the second drag");
    EXPECT((editor.undo() && asNumber(editor.document().get(rect).width) == 20.0), "and one more the first");
    EXPECT((editor.document().get(box).x == 10.0 && editor.document().get(box).y == 10.0), "and one undo gave the place back with the shape");

    // A circle's radius, and a ring's two lengths, grow evenly; stretching one way is refused and changes nothing.
    Document round;
    const PanelId circle = round.add(Panel{.name = "circle"});
    const OpRef<Circle> disc = round.add(circle, Circle{.radius = 10_cm});
    const PanelId skirt = round.add(Panel{.name = "skirt"});
    const OpRef<RingSector> ring = round.add(skirt, RingSector{.innerLength = 60_cm, .sweep = 180_deg, .depth = 30_cm});
    Editor roundEditor(round);
    drafting.handle(roundEditor, ScalePanel{circle, 1.0, 1.0, DragPhase::Begin});
    const Outcome oval = drafting.handle(roundEditor, ScalePanel{circle, 1.5, 1.0, DragPhase::Continue});
    EXPECT((oval.refused && oval.notice.find("evenly") != std::string::npos), "a circle is not stretched one way only");
    EXPECT((count<Scale>(roundEditor.document(), circle) == 0 && asNumber(roundEditor.document().get(disc).radius) == 10.0), "and nothing was changed or added");
    drafting.handle(roundEditor, ScalePanel{circle, 1.5, 1.5, DragPhase::End});
    EXPECT((asNumber(roundEditor.document().get(disc).radius) == 15.0 && count<Scale>(roundEditor.document(), circle) == 0), "but it is scaled evenly, by its radius");
    drafting.handle(roundEditor, ScalePanel{skirt, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(roundEditor, ScalePanel{skirt, 2.0, 2.0, DragPhase::End});
    EXPECT((asNumber(roundEditor.document().get(ring).innerLength) == 120.0 && asNumber(roundEditor.document().get(ring).depth) == 60.0 && asNumber(roundEditor.document().get(ring).sweep) == 180.0),
           "a ring's waist and depth double, its sweep stays");
}

TEST_CASE("a shape that a formula sets, or an instance, is scaled by a Scale layer, which the next drag keeps changing")
{
    Document doc;
    const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
    const PanelId box = doc.add(Panel{.name = "box", .x = 10, .y = 10});
    const OpRef<Rect> rect = doc.add(box, Rect{.width = wide, .height = 10_cm});
    const PanelId twin = doc.add(Panel{.name = "twin"});
    doc.add(twin, Instance{.source = box});
    Editor editor(doc);
    Drafting drafting;
    const auto scaleBy = [&](PanelId panel, double x, double y) {
        drafting.handle(editor, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
        drafting.handle(editor, ScalePanel{panel, x, y, DragPhase::Continue});
        return drafting.handle(editor, ScalePanel{panel, x, y, DragPhase::End});
    };
    scaleBy(box, 1.5, 1.0);
    EXPECT((count<Scale>(editor.document(), box) == 1), "the first drag adds a Scale at the end");
    EXPECT((!asNumber(editor.document().get(rect).width).has_value()), "the formula is still what sets the width");
    EXPECT_NEAR((editor.shapes()[box].length(rect.bottom())), (30.0), "half as wide again");
    EXPECT_NEAR((editor.shapes()[box].position(rect.bottomLeft()).x), (-5.0), "about the middle of its box");
    EXPECT((editor.document().get(box).x == 10.0), "the layer keeps the middle where it was, so the place stays");
    scaleBy(box, 1.0, 2.0);
    EXPECT((count<Scale>(editor.document(), box) == 1), "a second drag changes the same Scale");
    EXPECT((asNumber(last<Scale>(editor.document(), box).x) == 1.5 && asNumber(last<Scale>(editor.document(), box).y) == 2.0), "on top of what it had");
    EXPECT((editor.undo() && asNumber(last<Scale>(editor.document(), box).y) == 1.0), "one undo takes back the second drag");
    EXPECT((editor.undo() && count<Scale>(editor.document(), box) == 0), "and one more the first, with its layer");
    scaleBy(twin, 2.0, 2.0);
    EXPECT((count<Scale>(editor.document(), twin) == 1), "an instance has no lengths of its own: it is scaled by a layer too");

    // A Scale that a formula drives is not dragged.
    Document driven = doc;
    driven.add(box, Scale{.x = wide, .y = 1.0});
    Editor locked(driven);
    const Outcome refused = drafting.handle(locked, ScalePanel{box, 2.0, 2.0, DragPhase::Begin});
    EXPECT((refused.refused && refused.notice.find("formula") != std::string::npos), "it says a formula sets it");

    // A change that would break the outline is refused, and says why.
    Document round;
    const Param radius = round.add(Parameter{.name = "radius", .value = 10.0, .unit = Unit::Length});
    const PanelId circle = round.add(Panel{.name = "circle"});
    round.add(circle, Circle{.radius = radius});
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
    const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
    const PanelId box = doc.add(Panel{.name = "box"});
    doc.add(box, Rect{.width = wide, .height = 10_cm});
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
    const double height = *asNumber(editor.document().get(half.rect).height);
    drafting.handle(editor, ScalePanel{half.panel, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(editor, ScalePanel{half.panel, 1.0, 0.5, DragPhase::End});
    EXPECT((count<Scale>(editor.document(), half.panel) == 0 && asNumber(editor.document().get(half.rect).height) == height * 0.5),
           "the mirror applied last is looked past: the Rect under it is what changes");
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

    drafting.handle(editor, SelectCorners{front, {first}, Pick::Replace});
    const Outcome done = drafting.handle(editor, DeleteSelection{});
    EXPECT((!done.refused && editor.document().operations(front).size() == 2 && editor.document().operations(front)[1] == layers[2]), "that point's layer is dropped");
    EXPECT((drafting.selection.corners.empty() && drafting.selection.panel == front), "no corner is selected now, the panel still is");
    EXPECT((editor.undo() && editor.document().operations(front).size() == 3), "undo brings the point back");

    drafting.handle(editor, SelectCorners{front, {first, second}, Pick::Replace});
    drafting.handle(editor, DeleteSelection{});
    EXPECT((editor.document().operations(front).size() == 1), "two points go together");
    EXPECT((editor.undo() && editor.document().operations(front).size() == 3), "in one undo step");

    drafting.handle(editor, SelectCorners{front, {rect.topLeft()}, Pick::Replace});
    const std::size_t steps = editor.version();
    const Outcome refused = drafting.handle(editor, DeleteSelection{});
    EXPECT((refused.refused && refused.notice.find("1. Rectangle") != std::string::npos && refused.notice.find("Remove or change that layer") != std::string::npos),
           "a corner of the rectangle says which layer it belongs to");
    EXPECT((editor.version() == steps && drafting.selection.corners.size() == 1), "and nothing changes");

    drafting.handle(editor, SelectCorners{front, {first, rect.topLeft()}, Pick::Replace});
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
    EXPECT((drafting.copied().empty()), "nothing is copied at first");

    const Outcome nothing = drafting.handle(editor, PastePanel{Vec2{0, 0}});
    EXPECT((nothing.refused && !nothing.notice.empty() && !editor.canUndo()), "pasting before copying says so");

    const Outcome noneChosen = drafting.handle(editor, CopySelection{});
    EXPECT((noneChosen.refused && !noneChosen.notice.empty() && drafting.copied().empty()), "with nothing selected there is nothing to copy");
    drafting.handle(editor, SelectPanel{front});
    const Outcome copied = drafting.handle(editor, CopySelection{});
    EXPECT((!copied.refused && !copied.notice.empty() && drafting.copied() == std::vector<PanelId>{front} && !editor.canUndo()), "copying remembers the panel and changes no document");

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
    drafting.handle(editor, SelectPanel{front});
    drafting.handle(editor, CopySelection{});
    drafting.handle(editor, PastePanel{std::nullopt});
    const PanelId pasted = editor.document().panels()[1];
    EXPECT((editor.document().get(pasted).x > 20.0 && editor.document().get(pasted).y == 0.0), "just right of the original, as Duplicate does");

    drafting.handle(editor, DeletePanel{front});
    const std::uint64_t version = editor.version();
    const Outcome gone = drafting.handle(editor, PastePanel{Vec2{0, 0}});
    EXPECT((gone.refused && !gone.notice.empty() && editor.version() == version), "the panel is gone: a sentence, and no change");
    EXPECT((drafting.copied().empty()), "and nothing stays copied");
}

namespace {

// Three panels in a row: 20 by 10 cm each, at x = 0, 40 and 80.
struct Row {
    Document doc;
    PanelId a, b, c;
    Row() {
        a = doc.add(Panel{.name = "a", .x = 0, .y = 0});
        b = doc.add(Panel{.name = "b", .x = 40, .y = 0});
        c = doc.add(Panel{.name = "c", .x = 80, .y = 0});
        for (PanelId panel : {a, b, c}) doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    }
};

} // namespace

TEST_CASE("several panels are selected by Add and Toggle; the last one added is the one shown")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanel{row.a});
    drafting.handle(editor, SelectCorners{row.a, {OpRef<Rect>(editor.document().operations(row.a)[0]).topRight()}, Pick::Replace});
    drafting.handle(editor, SelectPanel{row.b, Pick::Add});
    EXPECT((drafting.selection.panel == row.b && drafting.selection.others == std::vector<PanelId>{row.a}), "Add makes the clicked panel the shown one and keeps the other");
    EXPECT((drafting.selection.corners.empty()), "the corners belonged to the panel that is no longer shown");
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.b, row.a} && drafting.selection.has(row.a) && !drafting.selection.has(row.c)), "panels() lists the shown one first");

    drafting.handle(editor, SelectPanel{row.b, Pick::Add});
    EXPECT((drafting.selection.panels().size() == 2), "Add of a panel already shown changes nothing");
    drafting.handle(editor, SelectPanel{row.a, Pick::Toggle});
    EXPECT((drafting.selection.panel == row.b && drafting.selection.others.empty()), "Toggle takes a selected panel out");
    drafting.handle(editor, SelectPanel{row.b, Pick::Toggle});
    EXPECT((!drafting.selection.panel && drafting.selection.others.empty()), "and the last one leaves nothing selected");

    drafting.handle(editor, SelectPanels{{row.a, row.b}, Pick::Replace});
    EXPECT((drafting.selection.panels().size() == 2 && drafting.selection.has(row.a) && drafting.selection.has(row.b)), "a box over two panels selects both");
    drafting.handle(editor, SelectPanels{{row.c}, Pick::Add});
    EXPECT((drafting.selection.panels().size() == 3), "a box with Add joins what is selected");
    drafting.handle(editor, SelectPanels{{row.a}, Pick::Replace});
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.a}), "a plain box replaces it");
    drafting.handle(editor, SelectPanels{{}, Pick::Replace});
    EXPECT((!drafting.selection.panel && drafting.selection.others.empty()), "a box over nothing chooses nothing");
    drafting.handle(editor, SelectPanels{{row.a, PanelId{999}}, Pick::Replace});
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.a}), "a panel that does not exist is not selected");

    drafting.handle(editor, SelectPanel{row.b});
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{row.b}), "a plain click on a panel replaces the whole selection");
    EXPECT((!editor.canUndo()), "selecting changes no document");
}

TEST_CASE("a selected panel that no longer exists is dropped, and another selected one is shown")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanels{{row.a, row.b, row.c}, Pick::Replace});
    const PanelId shown = *drafting.selection.panel;
    drafting.handle(editor, DeletePanel{shown});
    EXPECT((drafting.selection.panels().size() == 2 && !drafting.selection.has(shown) && drafting.selection.panel.has_value()), "a deleted panel leaves the others selected, one of them shown");
    editor.undo();
    REQUIRE(editor.change([&](Document& d) { d.remove(row.c); }, {.ifItBreaks = IfItBreaks::Allow}).has_value());
    drafting.forget(editor.document());
    EXPECT((!drafting.selection.has(row.c)), "forget drops a panel that is gone from the document");
}

TEST_CASE("Delete takes all the selected panels in one undo step; with corners selected it still takes only corners")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanels{{row.a, row.c}, Pick::Replace});
    const Outcome done = drafting.handle(editor, DeleteSelection{});
    EXPECT((!done.refused && editor.document().panels() == std::vector<PanelId>{row.b}), "both selected panels go; the other stays");
    EXPECT((!drafting.selection.panel && drafting.selection.others.empty()), "and nothing is selected any more");
    EXPECT((editor.undo() && editor.document().panels().size() == 3 && !editor.canUndo()), "one undo brings both back");

    // The 3D view shows no corners: Delete there takes the panels, whatever corners the table has selected.
    drafting.handle(editor, SelectPanels{{row.a, row.b}, Pick::Replace});
    drafting.handle(editor, SelectCorners{*drafting.selection.panel, {OpRef<Rect>(editor.document().operations(*drafting.selection.panel)[0]).topRight()}, Pick::Replace});
    const std::vector<PanelId> before = editor.document().panels();
    const Outcome corners = drafting.handle(editor, DeleteSelection{});
    EXPECT((corners.refused && editor.document().panels() == before), "with a corner of a shape selected, Delete refuses it and takes no panel");
    drafting.handle(editor, DeleteSelection{.wholePanels = true});
    EXPECT((editor.document().panels() == std::vector<PanelId>{row.c}), "wholePanels takes the panels instead");
}

TEST_CASE("copied panels are pasted as a group with their places in it kept, selected, in one undo step")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanels{{row.a, row.c}, Pick::Replace});
    const Outcome copied = drafting.handle(editor, CopySelection{});
    EXPECT((!copied.refused && drafting.copied() == std::vector<PanelId>{row.a, row.c} && !editor.canUndo()), "both are remembered, in the document's order, and nothing changes");

    drafting.handle(editor, PastePanel{Vec2{200, 50}});
    const std::vector<PanelId> panels = editor.document().panels();
    REQUIRE(panels.size() == 5);
    const Panel& first = editor.document().get(panels[3]);
    const Panel& second = editor.document().get(panels[4]);
    EXPECT((first.name == "a copy" && second.name == "c copy"), "each is named for its source");
    EXPECT_NEAR(second.x - first.x, 80.0, "they are as far apart as the originals were");
    EXPECT_NEAR(first.x + (80.0 + 20.0) / 2.0, 200.0, "the middle of the group's box is under the pointer in x");
    EXPECT_NEAR(first.y + 5.0, 50.0, "and in y");
    EXPECT((drafting.selection.panels() == std::vector<PanelId>{panels[3], panels[4]}), "the copies are selected");
    EXPECT((editor.undo() && editor.document().panels().size() == 3 && !editor.canUndo()), "a paste is one undo step");
    editor.redo();

    drafting.handle(editor, PastePanel{std::nullopt});
    const std::vector<PanelId> again = editor.document().panels();
    REQUIRE(again.size() == 7);
    EXPECT((editor.document().get(again[5]).x >= 0.0 + 100.0 && editor.document().get(again[5]).y == 0.0), "without a place the group goes just right of the originals");
    EXPECT((editor.document().get(again[5]).name == "a copy 2"), "with a free name");

    REQUIRE(editor.change([&](Document& d) { d.remove(row.a); }, {.ifItBreaks = IfItBreaks::Allow}).has_value());
    const std::size_t count = editor.document().panels().size();
    drafting.handle(editor, PastePanel{std::nullopt});
    EXPECT((editor.document().panels().size() == count + 1 && drafting.copied() == std::vector<PanelId>{row.c}), "a panel deleted since is left out; the rest are pasted");
}

TEST_CASE("several selected panels move together by as much as the one taken hold of, as one undo step")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanels{{row.a, row.b}, Pick::Replace});
    drafting.handle(editor, MovePanel{row.b, 40.0, 0.0, DragPhase::Begin});
    drafting.handle(editor, MovePanel{row.b, 52.0, -3.0, DragPhase::Continue});
    drafting.handle(editor, MovePanel{row.b, 52.0, -3.0, DragPhase::End});
    const Document& doc = editor.document();
    EXPECT((doc.get(row.b).x == 52.0 && doc.get(row.b).y == -3.0), "the panel taken hold of is where it was dragged");
    EXPECT((doc.get(row.a).x == 12.0 && doc.get(row.a).y == -3.0), "the other selected panel moved by the same");
    EXPECT((doc.get(row.c).x == 80.0 && doc.get(row.c).y == 0.0), "a panel not selected stays");
    EXPECT((editor.undo() && editor.document().get(row.a).x == 0.0 && editor.document().get(row.b).x == 40.0), "one undo puts both back");

    // A panel that is not selected is moved alone, even if others are selected.
    drafting.handle(editor, MovePanel{row.c, 80.0, 0.0, DragPhase::Begin});
    drafting.handle(editor, MovePanel{row.c, 90.0, 0.0, DragPhase::End});
    EXPECT((editor.document().get(row.c).x == 90.0 && editor.document().get(row.a).x == 0.0 && editor.document().get(row.b).x == 40.0), "a panel outside the selection moves alone");
}

TEST_CASE("several selected panels turn about the middle of the group; the shapes do not change")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanels{{row.a, row.b}, Pick::Replace});
    const std::optional<Vec2> middle = middleOfPanels(editor.document(), editor.shapes(), {row.a, row.b});
    REQUIRE(middle.has_value());
    EXPECT((std::abs(middle->x - 30.0) < 1e-9 && std::abs(middle->y - 5.0) < 1e-9), "the middle of the box that holds a (0 to 20) and b (40 to 60)");
    EXPECT((!middleOfPanels(editor.document(), editor.shapes(), {}).has_value()), "no panels have no middle");

    drafting.handle(editor, RotatePanel{row.a, 0.0, DragPhase::Begin});
    drafting.handle(editor, RotatePanel{row.a, 90.0, DragPhase::End});
    const Document& doc = editor.document();
    EXPECT((std::abs(doc.get(row.a).angle - 90.0) < 1e-9 && std::abs(doc.get(row.b).angle - 90.0) < 1e-9), "both turn by the angle given");
    // The middle is (30, 5): a's origin is 30 left and 5 below it, and a quarter turn counter-clockwise puts it 5 right and 30 below.
    EXPECT((std::abs(doc.get(row.a).x - 35.0) < 1e-9 && std::abs(doc.get(row.a).y + 25.0) < 1e-9), "a's origin turned about the middle of the group");
    EXPECT((std::abs(doc.get(row.b).x - 35.0) < 1e-9 && std::abs(doc.get(row.b).y - 15.0) < 1e-9), "and so is b's");
    EXPECT((std::abs(editor.shapes()[row.a].length(OpRef<Rect>(doc.operations(row.a)[0]).bottom()) - 20.0) < 1e-9), "turning is not shape");
    EXPECT((editor.undo() && editor.document().get(row.a).angle == 0.0 && editor.document().get(row.b).x == 40.0), "one undo turns them back");

}

TEST_CASE("several selected panels are scaled together about the middle of the group, evenly, one undo step")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanels{{row.a, row.b}, Pick::Replace});
    const auto widthOf = [&](PanelId panel) { return *asNumber(std::get<Rect>(editor.document().get(editor.document().operations(panel).front())).width); };

    drafting.handle(editor, ScalePanel{row.a, 1.0, 1.0, DragPhase::Begin});
    const Outcome uneven = drafting.handle(editor, ScalePanel{row.a, 1.5, 1.0, DragPhase::Continue});
    EXPECT((uneven.refused && !uneven.notice.empty() && !editor.canUndo()), "stretched one way, they are refused with a sentence and nothing changes");
    drafting.handle(editor, ScalePanel{row.a, 1.5, 1.5, DragPhase::Continue});
    drafting.handle(editor, ScalePanel{row.a, 1.5, 1.5, DragPhase::End});
    const Document& doc = editor.document();
    EXPECT((widthOf(row.a) == 30.0 && widthOf(row.b) == 30.0 && widthOf(row.c) == 20.0), "both selected panels are half as big again; the third is left");
    EXPECT((count<Scale>(doc, row.a) == 0 && count<Scale>(doc, row.b) == 0), "no layer is added to either");
    // The middle of the group is (30, 5): a's origin was 30 left of it and 5 below, and is now 1.5 times as far.
    EXPECT((std::abs(doc.get(row.a).x + 15.0) < 1e-9 && std::abs(doc.get(row.a).y + 2.5) < 1e-9), "a's place moved away from the middle of the group");
    EXPECT((std::abs(doc.get(row.b).x - 45.0) < 1e-9 && std::abs(doc.get(row.b).y + 2.5) < 1e-9), "and so did b's");
    const std::optional<Vec2> middle = middleOfPanels(doc, editor.shapes(), {row.a, row.b});
    EXPECT((middle && std::abs(middle->x - 30.0) < 1e-9 && std::abs(middle->y - 5.0) < 1e-9), "the middle of the group stays where it was");
    EXPECT((editor.undo() && widthOf(row.a) == 20.0 && widthOf(row.b) == 20.0 && editor.document().get(row.b).x == 40.0), "one undo puts both back");
}

TEST_CASE("a group scales a panel that a formula sets by a layer, and keeps the middle of the group where it was")
{
    Document doc;
    const Param wide = doc.add(Parameter{.name = "wide", .value = 20.0, .unit = Unit::Length});
    const PanelId a = doc.add(Panel{.name = "a", .x = 0, .y = 0});
    doc.add(a, Rect{.width = 20_cm, .height = 10_cm});
    const PanelId b = doc.add(Panel{.name = "b", .x = 40, .y = 0});
    doc.add(b, Rect{.width = wide, .height = 10_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, SelectPanels{{a, b}, Pick::Replace});
    const auto box = [&](PanelId panel) {
        const Bounds own = *bounds(editor.shapes()[panel]);
        const Panel& placed = editor.document().get(panel);
        return Bounds{{own.min.x + placed.x, own.min.y + placed.y}, {own.max.x + placed.x, own.max.y + placed.y}};
    };
    const Vec2 before = *middleOfPanels(editor.document(), editor.shapes(), {a, b});
    drafting.handle(editor, ScalePanel{a, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(editor, ScalePanel{a, 2.0, 2.0, DragPhase::End});
    EXPECT((count<Scale>(editor.document(), a) == 0 && count<Scale>(editor.document(), b) == 1), "a is scaled itself, b by a layer");
    const Vec2 after = *middleOfPanels(editor.document(), editor.shapes(), {a, b});
    EXPECT((std::abs(after.x - before.x) < 1e-9 && std::abs(after.y - before.y) < 1e-9), "the middle of the group does not move");
    EXPECT((std::abs(box(a).min.x + 30.0) < 1e-9 && std::abs(box(b).max.x - 90.0) < 1e-9), "the whole arrangement is twice as big about it: 0 to 60 becomes -30 to 90");
    EXPECT((std::abs(box(b).max.x - box(b).min.x - 40.0) < 1e-9), "b is twice as wide");
}

TEST_CASE("Duplicate copies the selected panels just right of the group, keeping their layout, as one undo step")
{
    Row row;
    Editor editor(row.doc);
    Drafting drafting;
    const Outcome nothing = drafting.handle(editor, DuplicateSelection{});
    EXPECT((nothing.refused && !nothing.notice.empty() && editor.document().panels().size() == 3), "with nothing selected it says what to do and changes nothing");

    drafting.handle(editor, SelectPanels{{row.a, row.b}, Pick::Replace});
    drafting.handle(editor, DuplicateSelection{});
    const Document& doc = editor.document();
    REQUIRE(doc.panels().size() == 5);
    const std::vector<PanelId> copies = drafting.selection.panels();
    REQUIRE(copies.size() == 2);
    EXPECT((!drafting.selection.has(row.a) && !drafting.selection.has(row.b)), "the copies are the selection now, not the originals");
    // The originals' box is 0 to 60; the copies go 12 cm beyond it, as far apart as the originals are.
    const PanelId first = doc.panels()[3], second = doc.panels()[4];
    EXPECT((std::abs(doc.get(first).x - 72.0) < 1e-9 && std::abs(doc.get(second).x - 112.0) < 1e-9 && doc.get(first).y == 0.0), "just right of the group, at the same distance from each other");
    EXPECT((doc.get(first).name == "a copy" && doc.get(second).name == "b copy"), "each named after its source");
    EXPECT((editor.undo() && editor.document().panels().size() == 3), "one undo takes both copies away");

    drafting.handle(editor, SelectPanel{row.c});
    drafting.handle(editor, DuplicateSelection{});
    EXPECT((editor.document().panels().size() == 4 && drafting.selection.panel && *drafting.selection.panel != row.c && drafting.selection.others.empty()), "one selected panel is duplicated as Add beside does");
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

TEST_CASE("double-clicking an edge that nothing sets gives it a length of its own, as long as it is now, one undo step")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "shirt"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -3_cm, .dy = -4_cm});  // the shoulder slopes: its length is nobody's setting
    Editor editor(doc);
    Drafting drafting;
    const double before = editor.shapes()[panel].length(rect.top());

    const Outcome typed = drafting.handle(editor, EditLength{panel, rect.top()});
    REQUIRE(typed.typeLength.has_value());
    EXPECT((count<SetLength>(editor.document(), panel) == 1 && std::holds_alternative<SetLength>(editor.document().get(typed.typeLength->operation))), "a length is added, and it is the one to type");
    EXPECT((typed.typeLength->field == FieldId{0} && typed.typeLength->label.find("Length of") == 0), "its one field, named for the edge");
    EXPECT((last<SetLength>(editor.document(), panel).edge == rect.top() && last<SetLength>(editor.document(), panel).keeping == SetLength::Keeping::Middle), "of the edge, moving both ends");
    EXPECT_NEAR((editor.shapes()[panel].length(rect.top())), (before), "the shape has not changed");
    EXPECT((editor.shapes().ok()), "and it is no problem");

    const Outcome again = drafting.handle(editor, EditLength{panel, rect.top()});
    EXPECT((again.typeLength && again.typeLength->operation == typed.typeLength->operation && count<SetLength>(editor.document(), panel) == 1), "the second double-click types into the same length, it adds none");

    REQUIRE(editor.change([&](Document& d) { d.set(typed.typeLength->operation, typed.typeLength->field, Expr(12_cm)); }));
    EXPECT_NEAR((editor.shapes()[panel].length(rect.top())), (12.0), "typing the number gives the edge that length");

    EXPECT((editor.undo() && editor.undo() && count<SetLength>(editor.document(), panel) == 0), "undo takes the typed length back, and one more the added layer");

    // A rectangle's top is its width's business, as it was.
    Document plain;
    const PanelId box = plain.add(Panel{.name = "box"});
    const OpRef<Rect> square = plain.add(box, Rect{.width = 20_cm, .height = 30_cm});
    Editor boxEditor(plain);
    const Outcome width = drafting.handle(boxEditor, EditLength{box, square.top()});
    EXPECT((width.typeLength && width.typeLength->operation == OpId(square) && count<SetLength>(boxEditor.document(), box) == 0), "an edge with a setting of its own still types that, and adds nothing");
}

TEST_CASE("an edge that cannot be given a length is refused with why, and no layer is left behind")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "panel"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {-2_cm, 3_cm}, .endHandle = {2_cm, 3_cm}});
    const PanelId disc = doc.add(Panel{.name = "disc"});
    const OpRef<Circle> circle = doc.add(disc, Circle{.radius = 10_cm});
    Editor editor(doc);
    Drafting drafting;

    const Outcome curve = drafting.handle(editor, EditLength{panel, rect.top()});
    EXPECT((!curve.typeLength && curve.refused && !curve.notice.empty() && count<SetLength>(editor.document(), panel) == 0), "a free curve says why");
    const Outcome round = drafting.handle(editor, EditLength{disc, circle.arc()});
    EXPECT((!round.typeLength && round.refused && !round.notice.empty() && count<SetLength>(editor.document(), disc) == 0), "so does a whole circle");
    EXPECT((editor.shapes().ok() && !editor.canUndo()), "nothing changed, nothing to undo");
}

TEST_CASE("on a symmetric panel the length is given to the drawn side and keeps the fold where it is")
{
    Half half;
    half.doc.add(half.panel, AdjustPoint{.corner = half.rect.topRight(), .dx = -2_cm, .dy = 0_cm});
    Editor editor(half.doc);
    Drafting drafting;

    const Outcome typed = drafting.handle(editor, EditLength{half.panel, half.fold.image(half.rect.top())});
    REQUIRE(typed.typeLength.has_value());
    EXPECT((last<SetLength>(editor.document(), half.panel).edge == half.rect.top()), "a double-click on the mirrored top makes the length of the drawn top");
    EXPECT((last<SetLength>(editor.document(), half.panel).keeping == SetLength::Keeping::End), "which keeps its end, the corner on the fold");
    REQUIRE(editor.change([&](Document& d) { d.set(typed.typeLength->operation, typed.typeLength->field, Expr(12_cm)); }));
    EXPECT_NEAR((editor.shapes()[half.panel].length(half.fold.image(half.rect.top()))), (12.0), "both halves of the top follow");
    EXPECT_NEAR((editor.shapes()[half.panel].position(half.rect.topLeft()).x), (0.0), "and the fold stays on the axis");

    const Outcome side = drafting.handle(editor, EditLength{half.panel, half.rect.right()});
    REQUIRE(side.typeLength.has_value());
    EXPECT((last<SetLength>(editor.document(), half.panel).edge == half.rect.right() && last<SetLength>(editor.document(), half.panel).keeping == SetLength::Keeping::Middle),
           "an edge away from the fold moves both its ends");
}

TEST_CASE("the scale gizmo does not scale a rectangle's numbers past a length given to one of its edges")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(panel, SetLength{.edge = rect.top(), .length = 30_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.handle(editor, ScalePanel{panel, 1.0, 1.0, DragPhase::Begin});
    drafting.handle(editor, ScalePanel{panel, 2.0, 2.0, DragPhase::Continue});
    drafting.handle(editor, ScalePanel{panel, 2.0, 2.0, DragPhase::End});
    EXPECT((count<Scale>(editor.document(), panel) == 1 && asNumber(editor.document().get(rect).width) == 20.0), "a Scale layer is added and the rectangle's own width is left alone");
    EXPECT_NEAR((editor.shapes()[panel].length(rect.top())), (60.0), "so the top is scaled with the rest");
    EXPECT_NEAR((editor.shapes()[panel].length(rect.bottom())), (40.0), "and so is the bottom");
}

TEST_CASE("the Length tool gives an edge a length of its own even where its shape sets it, and is then done")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 80_cm});
    Editor editor(doc);
    Drafting drafting;
    drafting.tool = Tool::Length;

    const Outcome made = drafting.handle(editor, EditLength{panel, rect.top(), true});
    REQUIRE(made.typeLength.has_value());
    EXPECT((count<SetLength>(editor.document(), panel) == 1 && last<SetLength>(editor.document(), panel).edge == rect.top()), "the top is given a length, though the rectangle's width sets it");
    EXPECT((made.toolDone && drafting.tool == Tool::Select), "the tool has done its work");
    EXPECT_NEAR((editor.shapes()[panel].length(rect.top())), (40.0), "the shape has not changed yet");

    REQUIRE(editor.change([&](Document& d) { d.set(made.typeLength->operation, made.typeLength->field, Expr(50_cm)); }));
    EXPECT_NEAR((editor.shapes()[panel].length(rect.top())), (50.0), "typing the waist widens the top");
    EXPECT_NEAR((editor.shapes()[panel].length(rect.bottom())), (40.0), "and the bottom keeps the rectangle's width");

    drafting.tool = Tool::Length;
    const Outcome again = drafting.handle(editor, EditLength{panel, rect.top(), true});
    EXPECT((again.typeLength && again.typeLength->operation == made.typeLength->operation && count<SetLength>(editor.document(), panel) == 1), "a second click types into the same length");

    const Outcome ankle = drafting.handle(editor, EditLength{panel, rect.bottom(), true});
    EXPECT((ankle.typeLength && count<SetLength>(editor.document(), panel) == 2), "and the bottom can be given its own");

    Document curved;
    const PanelId bent = curved.add(Panel{.name = "bent"});
    const OpRef<Rect> bentRect = curved.add(bent, Rect{.width = 20_cm, .height = 30_cm});
    curved.add(bent, Bezier{.edge = bentRect.top(), .startHandle = {-2_cm, 3_cm}, .endHandle = {2_cm, 3_cm}});
    Editor bentEditor(curved);
    drafting.tool = Tool::Length;
    const Outcome refused = drafting.handle(bentEditor, EditLength{bent, bentRect.top(), true});
    EXPECT((refused.refused && !refused.toolDone && drafting.tool == Tool::Length), "an edge that cannot have a length is refused, and the tool stays for another");
}

TEST_CASE("a length is the shape's setting or the edge's own, and either can be chosen again")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 80_cm});
    Editor editor(doc);
    Drafting drafting;

    const Outcome shape = drafting.handle(editor, EditLength{panel, rect.top()});
    REQUIRE(shape.typeLength.has_value());
    EXPECT((!shape.typeLength->ownLength && shape.typeLength->canSwitch && shape.typeLength->operation == OpId(rect)), "a rectangle's top is its width, and the window may offer an edge of its own");

    const Outcome own = drafting.handle(editor, EditLength{panel, rect.top(), true});
    REQUIRE(own.typeLength.has_value());
    EXPECT((own.typeLength->ownLength && own.typeLength->canSwitch && own.typeLength->keeping == SetLength::Keeping::Middle && own.typeLength->edge == rect.top()),
           "an own length says so, which end it keeps, and that the shape's setting can be chosen again");
    REQUIRE(editor.change([&](Document& d) { d.set(own.typeLength->operation, own.typeLength->field, Expr(55_cm)); }));
    EXPECT_NEAR((editor.shapes()[panel].length(rect.top())), (55.0), "the top has its own length");

    const Outcome back = drafting.handle(editor, UseShapeLength{panel, rect.top()});
    EXPECT((back.typeLength && back.typeLength->operation == OpId(rect) && !back.typeLength->ownLength && count<SetLength>(editor.document(), panel) == 0), "choosing the shape's setting removes the own length");
    EXPECT_NEAR((editor.shapes()[panel].length(rect.top())), (40.0), "and the top is the rectangle's width again");
    EXPECT((editor.undo() && count<SetLength>(editor.document(), panel) == 1), "in one undo step");

    // An edge whose corners were moved is nobody's setting; there is nothing to go back to.
    REQUIRE(editor.change([&](Document& d) { d.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = 3_cm, .dy = 0_cm}); }));
    const Outcome lost = drafting.handle(editor, UseShapeLength{panel, rect.top()});
    EXPECT((lost.refused && !lost.notice.empty() && count<SetLength>(editor.document(), panel) == 1), "when the shape's setting would not give the length, it says so and takes nothing away");

    Document shirt;
    const PanelId front = shirt.add(Panel{.name = "front"});
    const OpRef<Rect> body = shirt.add(front, Rect{.width = 20_cm, .height = 30_cm});
    shirt.add(front, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = -4_cm});
    Editor shirtEditor(shirt);
    const Outcome shoulder = drafting.handle(shirtEditor, EditLength{front, body.top()});
    EXPECT((shoulder.typeLength && shoulder.typeLength->ownLength && shoulder.typeLength->canSwitch), "an edge whose shape has a setting for it can switch, even after a corner was moved");
}

TEST_CASE("giving up an edge's own length is refused where the shape's setting would give it none, and the length stays")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "Back of shirt"});
    const OpRef<Rect> body = doc.add(front, Rect{.width = 40_cm, .height = 30_cm});
    doc.add(front, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = 0_cm});  // the shoulder: the shape no longer sets its length
    doc.add(front, SetLength{.edge = body.top(), .length = 30_cm});
    Editor editor(doc);
    Drafting drafting;
    const double before = editor.shapes()[front].length(body.top());

    const Outcome refused = drafting.handle(editor, UseShapeLength{front, body.top()});
    EXPECT((refused.refused && !refused.notice.empty() && count<SetLength>(editor.document(), front) == 1), "nothing is taken away, and it says why");
    EXPECT_NEAR((editor.shapes()[front].length(body.top())), (before), "the edge is as it was");
    EXPECT((refused.typeLength && refused.typeLength->ownLength && refused.typeLength->canSwitch), "and what is typed is still the edge's own length, for the window to show again");
    EXPECT((refused.typeLength->caption == "Length of top edge"), "the caption names the edge, not the panel, which may be called anything");
    EXPECT((!editor.canUndo()), "with nothing to undo");
}
