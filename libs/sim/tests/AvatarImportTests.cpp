// Unit tests of AvatarImport.cpp: a file's surface, in whatever unit and with
// whatever up, made a body that stands on the floor in cm, and the way back.

#include <algorithm>
#include <cmath>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/sim/AvatarImport.h"

using namespace clothcad;
using namespace clothcad::sim;

namespace {

// A box from `low` to `high`, faces counter-clockwise seen from outside.
Surface box(Vec3 low, Vec3 high) {
    Surface surface;
    for (int i = 0; i < 8; ++i)
        surface.points.push_back({i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z});
    surface.triangles = {{0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4},
                         {2, 6, 7}, {2, 7, 3}, {0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}};
    return surface;
}

// A body `tall` units high, y up, somewhere off the middle and off the floor.
Surface standingFigure(double tall) { return box({2.0, -0.5, 3.0}, {2.0 + tall * 0.3, -0.5 + tall, 3.0 + tall * 0.2}); }

// Six times the volume inside: positive when the faces look outward.
double volume(const Surface& surface) {
    double sum = 0.0;
    for (const auto& t : surface.triangles) sum += dot(surface.points[t[0]], cross(surface.points[t[1]], surface.points[t[2]]));
    return sum;
}

bool near(Vec3 a, Vec3 b, double within = 1e-9) { return length(a - b) < within; }

} // namespace

TEST_CASE("a body in metres stands a hundred times as tall as read as cm, on the floor in the middle")
{
    const Surface file = standingFigure(1.75);
    const AvatarImport how = standing(file, {.unit = 100.0});
    const Surface body = imported(file, how);
    const Vec3 size = sizeOf(body);
    EXPECT_NEAR(size.y, 175.0, "175 cm tall");
    EXPECT_NEAR(size.y, 100.0 * sizeOf(file).y, "a hundred times what the file's numbers say");
    EXPECT_NEAR(size.x, 52.5, "as wide");
    double low = 1e30;
    Vec3 first = body.points.front(), last = first;
    for (const Vec3& p : body.points) {
        low = std::min(low, p.y);
        first = {std::min(first.x, p.x), std::min(first.y, p.y), std::min(first.z, p.z)};
        last = {std::max(last.x, p.x), std::max(last.y, p.y), std::max(last.z, p.z)};
    }
    EXPECT_NEAR(low, 0.0, "the feet on the floor");
    EXPECT_NEAR(first.x + last.x, 0.0, "in the middle of x");
    EXPECT_NEAR(first.z + last.z, 0.0, "and of z");
    EXPECT((body.triangles == file.triangles && volume(body) > 0.0), "the same triangles, still facing out");
}

TEST_CASE("z up becomes y up, and what faced -y faces +z")
{
    // Tall along z, with a nose that sticks out towards -y.
    Surface file = box({-0.3, -0.1, 0.0}, {0.3, 0.1, 1.7});
    const std::size_t nose = file.points.size();
    file.points.push_back({0.0, -0.4, 1.6});
    file.points.push_back({0.1, 0.0, 1.6});  // to its side, towards +x
    const AvatarImport how = standing(file, {.unit = 100.0, .up = Axis::Z, .facing = Axis::MinusY});
    const Surface body = imported(file, how);
    EXPECT_NEAR(sizeOf(body).y, 170.0, "its height is up");
    EXPECT_NEAR(sizeOf(body).x, 60.0, "its width across");
    EXPECT(body.points[nose].z > 20.0, "the nose is in front");
    EXPECT(body.points[nose + 1].x > body.points[nose].x, "and what was towards +x still is: turned, not mirrored");
    EXPECT(volume(imported(box({0, 0, 0}, {1, 2, 3}), how)) > 0.0, "the faces still look out");
}

TEST_CASE("every pair of axes turns the body without mirroring it, and the way back finds the file's points")
{
    const Surface file = box({1.0, 2.0, 3.0}, {2.0, 4.0, 7.0});
    const Axis all[] = {Axis::X, Axis::Y, Axis::Z, Axis::MinusX, Axis::MinusY, Axis::MinusZ};
    bool outward = true, back = true;
    for (const Axis up : all)
        for (const Axis facing : all) {
            if (alongOneLine(up, facing)) continue;
            const AvatarImport how = standing(file, {.unit = 2.54, .rescale = 1.5, .up = up, .facing = facing});
            const Surface body = imported(file, how);
            outward = outward && volume(body) > 0.0 && std::abs(sizeOf(body).y - 2.54 * 1.5 * along(sizeOf(file), up)) < 1e-9;
            for (std::size_t i = 0; i < file.points.size(); ++i) back = back && near(inFile(how, body.points[i]), file.points[i]);
            back = back && near(directionInFile(how, directionInFile({}, {0, 1, 0})), directionInFile(how, {0, 1, 0}));
        }
    EXPECT(outward, "up is up, and inside stays inside");
    EXPECT(back, "back as it was");
    EXPECT(near(directionInFile({.unit = 100.0, .up = Axis::Z, .facing = Axis::MinusY, .moved = {1, 2, 3}}, {0, 1, 0}), {0, 0, 1}),
           "a direction is turned back, not scaled or moved");
}

TEST_CASE("a surface that is as it should be is left exactly as it is")
{
    const Surface file = box({-10, 0, -10}, {10, 170, 10});
    const AvatarImport how = standing(file, {});
    EXPECT(how == AvatarImport{}, "nothing to do");
    const Surface body = imported(file, how);
    bool same = true;
    for (std::size_t i = 0; i < file.points.size(); ++i) same = same && length(body.points[i] - file.points[i]) == 0.0;
    EXPECT(same, "to the last digit");
}

TEST_CASE("the guess picks the unit in which the body is as tall as a person")
{
    const auto is = [](double tall, double unit) {
        const AvatarImport how = guessImport(standingFigure(tall));
        return how.unit == unit && how.rescale == 1.0;
    };
    EXPECT(is(1.75, 100.0), "1.75 tall: metres, as it is");
    EXPECT(is(175.0, 1.0), "175: cm");
    EXPECT(is(1750.0, 0.1), "1750: mm");
    EXPECT(is(69.0, 2.54), "69: inches");
    EXPECT(is(157.7, 1.0), "a small person in cm");
    EXPECT((is(1.2, 100.0) && is(2.3, 100.0)), "from 120 to 230 cm");
    const Surface file = standingFigure(1.75);
    EXPECT(guessImport(file) == standing(file, guessImport(file)), "and it stands");
}

TEST_CASE("a body that is as tall as a person in no unit is taken in the nearest one and made 170 cm")
{
    const AvatarImport how = guessImport(standingFigure(1.004));  // made-up models are often about 1 tall
    EXPECT_NEAR(how.unit, 100.0, "metres come nearest");
    EXPECT_NEAR(how.rescale, 170.0 / 100.4, "and the rest is a scale");
    EXPECT_NEAR(sizeOf(imported(standingFigure(1.004), how)).y, 170.0, "170 cm");
    EXPECT_NEAR(guessImport(standingFigure(12.0)).unit, 2.54, "12: inches come nearest");
    EXPECT_NEAR(sizeOf(imported(standingFigure(12.0), guessImport(standingFigure(12.0)))).y, 170.0, "whatever it is");
    EXPECT_NEAR(guessImport(Surface{}).cmPerUnit(), 1.0, "nothing to measure: as it is");
    EXPECT(guessImport(Surface{}) == AvatarImport{}, "and nowhere to move");
}

TEST_CASE("the guess takes the longest side as up, y and then z before one barely longer")
{
    const auto up = [](Vec3 size) { return guessImport(box({0, 0, 0}, size)).up; };
    EXPECT(up({50, 170, 30}) == Axis::Y, "tall along y");
    EXPECT(up({50, 30, 170}) == Axis::Z, "along z");
    EXPECT(up({170, 50, 30}) == Axis::X, "along x");
    EXPECT(up({180, 170, 30}) == Axis::Y, "arms held out, wider than tall");
    EXPECT(up({180, 30, 170}) == Axis::Z, "the same with z up");
    EXPECT(up({0.69, 1.0040, 1.0037}) == Axis::Y, "as deep as tall");
    EXPECT(guessImport(box({0, 0, 0}, {50, 30, 170})).facing == Axis::MinusY, "with z up a body faces -y");
    EXPECT(guessImport(box({0, 0, 0}, {50, 170, 30})).facing == Axis::Z, "with y up, +z");
}

TEST_CASE("a usual facing never lies along the axis that is up")
{
    for (const Axis up : {Axis::X, Axis::Y, Axis::Z, Axis::MinusX, Axis::MinusY, Axis::MinusZ})
        EXPECT(!alongOneLine(up, usualFacing(up)), "it can be chosen");
}
