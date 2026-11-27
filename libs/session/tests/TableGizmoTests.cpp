// Unit tests of TableGizmo.cpp: the table's move, rotate and scale gizmos:
// which handle is under the pointer and what dragging it does.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/session/TableGizmo.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

// Two pixels to the cm, standing at (10, 20).
constexpr double cmPerPixel = 0.5;
constexpr double arrow = tableArrowPixels * cmPerPixel, ring = tableRingPixels * cmPerPixel;
TableGizmo gizmo(GizmoMode mode, double angle = 0.0) { return {.mode = mode, .centre = {10.0, 20.0}, .angle = angle, .cmPerPixel = cmPerPixel}; }

} // namespace

TEST_CASE("the table's gizmo is big enough to take hold of comfortably")
{
    EXPECT((tableArrowPixels >= 100.0 && tableRingPixels >= 80.0), "arrows of at least 100 pixels, a ring of at least 80");
}

TEST_CASE("the move gizmo's middle and arrows can be taken hold of, and move along their axis only")
{
    const TableGizmo move = gizmo(GizmoMode::Move, 30.0);
    EXPECT((handleAt(move, {10.5, 20.5}) == TableHandle::Middle), "the middle square");
    EXPECT((handleAt(move, {10.0 + arrow * 0.6, 20.0}) == TableHandle::AlongX), "the x arrow, along the table's x whatever the panel's angle");
    EXPECT((handleAt(move, {10.0, 20.0 + arrow * 0.6}) == TableHandle::AlongY), "the y arrow");
    EXPECT((!handleAt(move, {10.0 + arrow * 0.6, 20.0 + arrow * 0.6}).has_value()), "between them, nothing: the middle moves freely already");
    EXPECT((!handleAt(move, {10.0 + arrow * 1.3, 20.0}).has_value()), "past the end of an arrow, nothing");

    const Vec2 alongX = moveBy(move, TableHandle::AlongX, {30.0, 20.0}, {36.0, 27.0});
    EXPECT((alongX.x == 6.0 && alongX.y == 0.0), "the x arrow moves along x only");
    const Vec2 alongY = moveBy(move, TableHandle::AlongY, {10.0, 45.0}, {16.0, 41.0});
    EXPECT((alongY.x == 0.0 && alongY.y == -4.0), "the y arrow along y only");
    const Vec2 free = moveBy(move, TableHandle::Middle, {10.0, 20.0}, {13.0, 18.0});
    EXPECT((free.x == 3.0 && free.y == -2.0), "the middle moves with the pointer");
}

TEST_CASE("the rotate gizmo is a ring, and turns by the angle swept about the middle")
{
    const TableGizmo turn = gizmo(GizmoMode::Rotate);
    EXPECT((handleAt(turn, {10.0 + ring, 20.0}) == TableHandle::Ring), "on the ring");
    EXPECT((handleAt(turn, {10.0, 20.0 - ring - 1.0}) == TableHandle::Ring), "anywhere round it");
    EXPECT((!handleAt(turn, {10.0, 20.0}).has_value()), "not in the middle");
    EXPECT_NEAR((turnBy(turn, {10.0 + ring, 20.0}, {10.0, 20.0 + ring})), (90.0), "from its right to its top is a quarter turn counter-clockwise");
    EXPECT_NEAR((turnBy(turn, {10.0 + ring, 20.0}, {10.0, 20.0 - ring})), (-90.0), "to its bottom, clockwise");
}

TEST_CASE("the scale gizmo follows the panel's own axes; the square between them and the middle scale evenly")
{
    const TableGizmo scale = gizmo(GizmoMode::Scale, 90.0);  // turned a quarter: its own x points up the table
    const Vec2 xEnd = handleEnd(scale, TableHandle::AlongX);
    EXPECT((std::abs(xEnd.x - 10.0) < 1e-9 && std::abs(xEnd.y - (20.0 + arrow)) < 1e-9), "the x handle points along the panel's own x");
    EXPECT((handleAt(scale, {10.0, 20.0 + arrow * 0.8}) == TableHandle::AlongX), "and is taken hold of there");

    const Factors twice = scaleBy(scale, TableHandle::AlongX, {10.0, 40.0}, {10.0, 60.0});
    EXPECT((std::abs(twice.x - 2.0) < 1e-9 && twice.y == 1.0), "twice as far from the middle along x is twice as wide");
    const Factors half = scaleBy(scale, TableHandle::AlongY, {-10.0, 20.0}, {0.0, 25.0});
    EXPECT((half.x == 1.0 && std::abs(half.y - 0.5) < 1e-9), "half as far along y is half as tall");
    const Factors never = scaleBy(scale, TableHandle::AlongX, {10.0, 40.0}, {10.0, -40.0});
    EXPECT((never.x == 0.01), "past the middle it does not turn inside out");

    // The square between the panel's own x (up the table) and y (to the left).
    Vec2 square{};
    for (const Vec2& corner : planeCorners(scale)) square = {square.x + corner.x / 4.0, square.y + corner.y / 4.0};
    EXPECT((std::abs(square.x - (10.0 - 0.3 * arrow)) < 1e-9 && std::abs(square.y - (20.0 + 0.3 * arrow)) < 1e-9), "the square stands between x and y");
    EXPECT((handleAt(scale, square) == TableHandle::Plane), "and is taken hold of there");
    EXPECT((!handleAt(gizmo(GizmoMode::Move), {10.0 + 0.3 * arrow, 20.0 + 0.3 * arrow}).has_value()), "the move gizmo has no such square");
    const Factors both = scaleBy(scale, TableHandle::Plane, square, {10.0 + (square.x - 10.0) * 1.5, 20.0 + (square.y - 20.0) * 1.5});
    EXPECT((std::abs(both.x - 1.5) < 1e-9 && both.x == both.y), "half as far again from the middle: x and y both half as big again");

    const Factors even = scaleBy(scale, TableHandle::Middle, {10.0, 20.0}, {35.0, 0.0});
    EXPECT((std::abs(even.x - 1.5) < 1e-9 && even.x == even.y), "the middle: 50 pixels to the right is half as big again, both ways");
}
