// Unit tests of Locks.cpp: what may not be dragged, because a formula decides it.

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/session/Locks.h"

using namespace clothcad;

TEST_CASE("the ends of an edge whose length a formula gives are locked; the other corners are not")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const auto lockOf = [&](const CornerRef& corner) { return session::cornerLock(doc, evaluate(doc), panel, corner); };
    EXPECT((lockOf(rect.topRight()).empty() && lockOf(rect.topLeft()).empty()), "nothing is locked while the shape is plain numbers");

    const OpRef<SetLength> top = doc.add(panel, SetLength{.edge = rect.top(), .length = waist / 2});
    EXPECT((lockOf(rect.topRight()).find("formula") != std::string::npos && lockOf(rect.topLeft()).find("formula") != std::string::npos),
           "both ends of the top are locked, and it says a formula is why");
    EXPECT((lockOf(rect.bottomRight()).empty() && lockOf(rect.bottomLeft()).empty()), "the other corners can still be dragged");

    doc.setEnabled(top, false);
    EXPECT((lockOf(rect.topRight()).empty()), "a length that is switched off locks nothing");
    doc.setEnabled(top, true);
    doc.set(top, &SetLength::length, Expr(35_cm));
    EXPECT((lockOf(rect.topRight()).empty() && lockOf(rect.topLeft()).empty()), "a length typed as a number does not lock: dragging then simply changes it");
}

TEST_CASE("on a symmetric panel the mirror image of a locked corner is locked too")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId panel = doc.add(Panel{.name = "half"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    const OpRef<Mirror> fold = doc.add(panel, Mirror{.across = rect.left()});
    doc.add(panel, SetLength{.edge = rect.top(), .length = waist / 4, .keeping = SetLength::Keeping::End});
    const Result shapes = evaluate(doc);
    EXPECT((!session::cornerLock(doc, shapes, panel, rect.topRight()).empty()), "the drawn end is locked");
    EXPECT((!session::cornerLock(doc, shapes, panel, fold.image(rect.topRight())).empty()), "and so is its mirror image");
    EXPECT((session::cornerLock(doc, shapes, panel, rect.bottomRight()).empty()), "a corner of no such edge is free");
}

TEST_CASE("only the length that is in force locks: a formula that a later length or a moved corner has overruled holds nothing")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const OpRef<SetLength> formula = doc.add(panel, SetLength{.edge = rect.top(), .length = waist / 2});
    const auto lockOf = [&](const CornerRef& corner) { return session::cornerLock(doc, evaluate(doc), panel, corner); };
    EXPECT((!lockOf(rect.topRight()).empty()), "the formula holds the ends while it is the length in force");

    const OpRef<SetLength> typed = doc.add(panel, SetLength{.edge = rect.top(), .length = 40_cm});
    EXPECT((lockOf(rect.topRight()).empty() && lockOf(rect.topLeft()).empty()), "a later length of the same edge wins: the corners are free, and not told to change a formula that has no effect");
    doc.remove(typed);
    EXPECT((!lockOf(rect.topRight()).empty()), "and the formula holds them again once that is gone");

    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = 3_cm, .dy = 0_cm});
    EXPECT((lockOf(rect.topRight()).empty() && lockOf(rect.topLeft()).empty()), "a corner moved after it leaves the formula with nothing to hold");
    (void)formula;
}
