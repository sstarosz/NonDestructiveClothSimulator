// Unit tests of Picking.cpp: what is under a place in the 3D view's picture.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/render/Picking.h"

using namespace twintailor;
using namespace twintailor::render;

namespace {

// A camera looking straight at the point (0, 100, 0) from the front.
OrbitCamera level() {
    OrbitCamera camera;
    camera.target = {0.0, 100.0, 0.0};
    camera.pitch = 0.0;
    camera.yaw = 0.0;
    camera.distance = 400.0;
    return camera;
}

// A square facing the camera, `size` cm across, centred on (0, 100, z).
Surface square(float size, float z, std::optional<PanelId> panel) {
    const float h = size / 2.0f;
    return {.points = {{-h, 100.0f - h, z}, {h, 100.0f - h, z}, {h, 100.0f + h, z}, {-h, 100.0f + h, z}},
            .normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}},
            .triangles = {0, 1, 2, 0, 2, 3},
            .colour = {},
            .panel = panel};
}

constexpr ViewSize size{.width = 1200, .height = 900};

} // namespace

TEST_CASE("a ray through a place in the picture comes back to that place")
{
    OrbitCamera camera = level();
    camera.yaw = 30.0;
    camera.pitch = 20.0;
    for (const PicturePoint at : {PicturePoint{600, 450}, PicturePoint{100, 50}, PicturePoint{1100, 800}}) {
        const Ray ray = rayThrough(camera, size, at);
        const std::optional<PicturePoint> back = pictureOf(camera, size, ray.from + ray.direction * 250.0);
        REQUIRE(back.has_value());
        EXPECT((std::abs(back->x - at.x) < 1e-3 && std::abs(back->y - at.y) < 1e-3), "a point along the ray is drawn where the ray started");
    }
    EXPECT((!pictureOf(camera, size, camera.eye() + camera.toEye() * 10.0).has_value()), "a point behind the camera is drawn nowhere");
}

TEST_CASE("the panel under the mouse is found, unless the body is in front of it")
{
    const OrbitCamera camera = level();
    Scene scene;
    scene.surfaces.push_back(square(40.0f, 0.0f, PanelId{7}));
    const std::optional<PanelId> middle = panelAt(scene, camera, size, {600, 450});
    EXPECT((middle == PanelId{7}), "the middle of the picture is on the panel");
    EXPECT((!panelAt(scene, camera, size, {20, 20}).has_value()), "a corner of the picture is on nothing");

    scene.surfaces.push_back(square(10.0f, 30.0f, std::nullopt));  // the body, nearer the camera
    EXPECT((!panelAt(scene, camera, size, {600, 450}).has_value()), "where the body is in front, no panel is under the mouse");
    const std::optional<PicturePoint> beside = pictureOf(camera, size, {15.0, 100.0, 0.0});
    REQUIRE(beside.has_value());
    EXPECT((panelAt(scene, camera, size, *beside) == PanelId{7}), "beside the body the panel is under the mouse again");

    // The same with the body's geometry in a shared shape.
    const Surface nearer = square(10.0f, 30.0f, std::nullopt);
    Surface shaped{.points = {}, .normals = {}, .triangles = {}, .colour = {}};
    shaped.shape = std::make_shared<const Shape>(Shape{.points = nearer.points, .normals = nearer.normals, .triangles = nearer.triangles});
    scene.surfaces.back() = shaped;
    EXPECT((!panelAt(scene, camera, size, {600, 450}) && panelAt(scene, camera, size, *beside) == PanelId{7}), "a shape is in the way as any surface is");

    scene.surfaces.clear();
    scene.overlay.push_back(square(40.0f, 0.0f, PanelId{7}));
    EXPECT((!panelAt(scene, camera, size, {600, 450}).has_value()), "the overlay is not picked");
}

TEST_CASE("through a see-through body the cloth behind it is under the mouse")
{
    const OrbitCamera camera = level();
    Scene scene;
    Surface body = square(40.0f, 30.0f, std::nullopt);  // nearer the camera than the cloth
    body.colour.alpha = 0.35f;
    body.nearestOnly = true;
    scene.surfaces = {body, square(10.0f, 0.0f, PanelId{7})};
    EXPECT((panelAt(scene, camera, size, {600, 450}) == PanelId{7}), "what shows through the body can be clicked");
    const std::optional<ClothHit> hit = clothAt(scene, camera, size, {600, 450});
    EXPECT((hit && hit->panel == PanelId{7}), "and pulled");
    const std::optional<PicturePoint> bodyAlone = pictureOf(camera, size, {15.0, 100.0, 30.0});
    REQUIRE(bodyAlone.has_value());
    EXPECT((!panelAt(scene, camera, size, *bodyAlone) && !clothAt(scene, camera, size, *bodyAlone)), "with no cloth behind it, there is nothing there");
}

TEST_CASE("the cloth under the mouse gives the spot and the nearest particle")
{
    const OrbitCamera camera = level();
    Scene scene;
    scene.surfaces.push_back(square(40.0f, 0.0f, PanelId{3}));
    const std::optional<PicturePoint> nearCorner = pictureOf(camera, size, {15.0, 112.0, 0.0});  // near the corner (20, 120)
    REQUIRE(nearCorner.has_value());
    const std::optional<ClothHit> hit = clothAt(scene, camera, size, *nearCorner);
    REQUIRE(hit.has_value());
    EXPECT((hit->panel == PanelId{3}), "it is the panel");
    EXPECT((hit->point == 2u), "the particle nearest the spot is the top right corner");
    EXPECT((sim::length(hit->at - sim::Vec3{15.0, 112.0, 0.0}) < 1e-6), "the spot is where the ray meets the cloth");
}

TEST_CASE("a hand moved across the view keeps the spot it holds under the mouse")
{
    OrbitCamera camera = level();
    camera.yaw = 40.0;
    camera.pitch = 25.0;
    const sim::Vec3 held{10.0, 90.0, 5.0};
    const PicturePoint pressed = *pictureOf(camera, size, held);
    const PicturePoint now{pressed.x + 80.0, pressed.y - 30.0};
    const std::optional<sim::Vec3> by = acrossTheView(camera, size, held, pressed, now);
    REQUIRE(by.has_value());
    const PicturePoint after = *pictureOf(camera, size, held + *by);
    EXPECT((std::abs(after.x - now.x) < 1e-6 && std::abs(after.y - now.y) < 1e-6), "the moved spot is drawn under the mouse");
    EXPECT((std::abs(sim::dot(*by, camera.toEye())) < 1e-9), "it moves in the plane facing the camera");
}
