// Unit tests of Gizmo.cpp: the move gizmo's handles, how far a drag moves a
// panel, and what is drawn.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/render/Gizmo.h"

using namespace clothcad;
using namespace clothcad::render;

namespace {

OrbitCamera aslant() {
    OrbitCamera camera;
    camera.target = {0.0, 100.0, 0.0};
    camera.yaw = 25.0;
    camera.pitch = 15.0;
    camera.distance = 400.0;
    return camera;
}

constexpr ViewSize size{.width = 1200, .height = 900};
const Gizmo gizmo{.centre = {0.0, 100.0, 0.0}, .pixel = 1.0};

PicturePoint drawnAt(const OrbitCamera& camera, sim::Vec3 point) { return *pictureOf(camera, size, point); }

bool near(sim::Vec3 a, sim::Vec3 b, double within) { return sim::length(a - b) < within; }

} // namespace

TEST_CASE("the gizmo's middle and each arrow can be taken hold of")
{
    const OrbitCamera camera = aslant();
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, gizmo.centre)) == GizmoHandle::AcrossTheView), "the middle square");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {10.0, 100.0, 0.0})) == GizmoHandle::AlongX), "the x arrow");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {0.0, 110.0, 0.0})) == GizmoHandle::AlongY), "the y arrow");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {0.0, 100.0, 10.0})) == GizmoHandle::AlongZ), "the z arrow");
    EXPECT((!handleAt(gizmo, camera, size, {20.0, 20.0}).has_value()), "far from it, nothing");
}

TEST_CASE("an arrow pointing at the eye is not shown and cannot be taken hold of")
{
    OrbitCamera camera = aslant();
    camera.yaw = 0.0;
    camera.pitch = 0.0;  // looking along -z: the z arrow points straight at the eye
    Scene scene;
    addGizmo(scene, gizmo, camera, size, std::nullopt);
    EXPECT((scene.overlay.size() == 3), "two arrows and the middle");
    EXPECT((scene.surfaces.empty() && scene.lines.empty()), "the gizmo goes only into the overlay");
    EXPECT((handleAt(gizmo, camera, size, drawnAt(camera, {0.0, 100.0, 10.0})) != GizmoHandle::AlongZ), "the z arrow is not there to take");
}

TEST_CASE("the gizmo is as big on the screen near and far, and bigger on a denser screen")
{
    OrbitCamera camera = aslant();
    Scene near_, far_, dense;
    addGizmo(near_, gizmo, camera, size, std::nullopt);
    camera.distance = 1200.0;
    addGizmo(far_, gizmo, camera, size, std::nullopt);
    addGizmo(dense, Gizmo{.centre = gizmo.centre, .pixel = 2.0}, camera, size, std::nullopt);
    const auto reach = [](const Scene& scene) {
        double most = 0.0;
        for (const Point& p : scene.overlay.front().points) most = std::max(most, static_cast<double>(p.x));
        return most;
    };
    EXPECT((std::abs(reach(far_) / reach(near_) - 3.0) < 0.05), "three times as far, three times as long in space");
    EXPECT((std::abs(reach(dense) / reach(far_) - 2.0) < 0.05), "twice the pixels, twice as long");
}

TEST_CASE("the handle held is drawn in the selection colour")
{
    const OrbitCamera camera = aslant();
    Scene scene;
    addGizmo(scene, gizmo, camera, size, GizmoHandle::AlongY);
    REQUIRE(scene.overlay.size() == 4);
    const Colour y = scene.overlay[1].colour, x = scene.overlay[0].colour;
    EXPECT((y.red == 1.0f && y.green == 0.75f), "the y arrow is amber");
    EXPECT((x.red != 1.0f), "the x arrow keeps its own colour");
}

TEST_CASE("dragging an arrow moves along its axis only; the middle moves across the view")
{
    const OrbitCamera camera = aslant();
    const std::optional<sim::Vec3> alongX =
        moveBy(gizmo, GizmoHandle::AlongX, camera, size, drawnAt(camera, {10.0, 100.0, 0.0}), drawnAt(camera, {30.0, 100.0, 0.0}), 0.0);
    REQUIRE(alongX.has_value());
    EXPECT(near(*alongX, {20.0, 0.0, 0.0}, 1e-6), "the x arrow pulled 20 cm along x moves 20 cm along x");

    // The mouse moved up the screen while holding the x arrow: x hardly
    // changes, and nothing else does.
    const PicturePoint pressed = drawnAt(camera, {10.0, 100.0, 0.0});
    const std::optional<sim::Vec3> sideways = moveBy(gizmo, GizmoHandle::AlongX, camera, size, pressed, {pressed.x, pressed.y - 100.0}, 0.0);
    REQUIRE(sideways.has_value());
    EXPECT((sideways->y == 0.0 && sideways->z == 0.0), "the y arrow's and z's values stay");

    const std::optional<sim::Vec3> snapped =
        moveBy(gizmo, GizmoHandle::AlongY, camera, size, drawnAt(camera, {0.0, 110.0, 0.0}), drawnAt(camera, {0.0, 122.34, 0.0}), 1.0);
    REQUIRE(snapped.has_value());
    EXPECT(near(*snapped, {0.0, 12.0, 0.0}, 1e-9), "with a step of 1 cm the move is in whole centimetres");

    const sim::Vec3 side = sim::normalized(sim::cross(camera.toEye() * -1.0, {0.0, 1.0, 0.0}));
    const std::optional<sim::Vec3> across =
        moveBy(gizmo, GizmoHandle::AcrossTheView, camera, size, drawnAt(camera, gizmo.centre), drawnAt(camera, gizmo.centre + side * 15.0), 0.0);
    REQUIRE(across.has_value());
    EXPECT(near(*across, side * 15.0, 1e-6), "the middle moves in the plane facing the camera, under the mouse");
}

TEST_CASE("the gizmo stands where a panel's arrangement places it")
{
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front"});
    doc.add(front, Rect{.width = 40_cm, .height = 60_cm});
    doc.arrange(front, Arrangement{.placed = true, .x = 5, .y = 110, .z = 25});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.update();
    const std::optional<sim::Vec3> centre = gizmoCentre(session, front);
    REQUIRE(centre.has_value());
    EXPECT(near(*centre, {5.0, 110.0, 25.0}, 1e-9), "at x, y and z of the arrangement");
    EXPECT((!gizmoCentre(session, PanelId{999}).has_value()), "an unknown panel has none");
}

TEST_CASE("the axes in the corner stay in the bottom left, x red, y green, z blue, however the camera turns")
{
    for (const double yaw : {0.0, 90.0, -135.0}) {
        OrbitCamera camera = aslant();
        camera.yaw = yaw;
        camera.distance = yaw == 90.0 ? 150.0 : 900.0;
        Scene scene;
        addCornerAxes(scene, camera, size, 1.0);
        REQUIRE(scene.overlay.size() == 3);
        EXPECT((scene.surfaces.empty()), "in the overlay only: it is never picked");
        EXPECT((scene.overlay[0].colour.red > scene.overlay[0].colour.green && scene.overlay[1].colour.green > scene.overlay[1].colour.red &&
                scene.overlay[2].colour.blue > scene.overlay[2].colour.red),
               "x red, y green, z blue");
        bool inCorner = true;
        for (const Surface& arrow : scene.overlay)
            for (const Point& p : arrow.points) {
                const std::optional<PicturePoint> at = pictureOf(camera, size, {p.x, p.y, p.z});
                inCorner = inCorner && at && at->x > 0.0 && at->x < 80.0 && at->y > size.height - 80.0 && at->y < size.height;
            }
        EXPECT(inCorner, "every point of them is drawn within 80 pixels of the bottom left corner");
    }
}
