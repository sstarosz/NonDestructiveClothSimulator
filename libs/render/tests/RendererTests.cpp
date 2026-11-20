// Unit tests of Renderer.cpp: pictures drawn off screen and read back. They
// use whatever Vulkan the machine has (a software one such as lavapipe is
// enough) and are skipped where there is none.

#include <cmath>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/render/Renderer.h"
#include "clothcad/render/SceneOf.h"

using namespace clothcad;
using namespace clothcad::render;

namespace {

// One renderer for all the tests: starting Vulkan takes a moment.
Renderer* renderer() {
    static std::expected<std::unique_ptr<Renderer>, std::string> made = Renderer::create({});
    return made ? made->get() : nullptr;
}

std::string whyNoVulkan() {
    const auto made = Renderer::create({});
    return made ? std::string() : made.error();
}

bool near(Colour a, Colour b) { return std::abs(a.red - b.red) < 0.02f && std::abs(a.green - b.green) < 0.02f && std::abs(a.blue - b.blue) < 0.02f; }

// A square facing the camera, `size` cm across, at depth z.
Surface square(float size, float z, Colour colour) {
    const float h = size / 2.0f;
    return {.points = {{-h, 100.0f - h, z}, {h, 100.0f - h, z}, {h, 100.0f + h, z}, {-h, 100.0f + h, z}},
            .normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}},
            .triangles = {0, 1, 2, 0, 2, 3},
            .colour = colour};
}

Scene facing() {
    OrbitCamera camera;
    camera.pitch = 0.0;
    Scene scene;
    scene.viewProjection = camera.viewProjection(4.0 / 3.0);
    scene.toLight = {0.0f, 0.0f, 1.0f};
    return scene;
}

} // namespace

TEST_CASE("an empty scene is the background")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    const auto picture = renderer()->draw(scene, 64, 48);
    REQUIRE(picture);
    EXPECT((picture->width == 64 && picture->pixels.size() == 64u * 48u * 4u), "a picture of the size asked for");
    EXPECT((near(picture->at(0, 0), scene.background) && near(picture->at(63, 47), scene.background)), "all of it the background");
}

TEST_CASE("a surface facing the light is drawn in its colour, the nearer in front")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    scene.surfaces.push_back(square(60.0f, 0.0f, {1.0f, 0.0f, 0.0f}));  // about 29 pixels across, 400 cm away
    scene.surfaces.push_back(square(20.0f, 20.0f, {0.0f, 1.0f, 0.0f}));  // nearer the camera, drawn second
    const auto picture = renderer()->draw(scene, 160, 120);
    REQUIRE(picture);
    EXPECT((near(picture->at(80, 60), {0.0f, 1.0f, 0.0f})), "the near square covers the middle");
    EXPECT((near(picture->at(80 + 10, 60), {1.0f, 0.0f, 0.0f})), "the far one shows around it, fully lit");
    EXPECT((near(picture->at(2, 2), scene.background)), "and the corners are empty");

    std::swap(scene.surfaces[0], scene.surfaces[1]);
    const auto reordered = renderer()->draw(scene, 160, 120);
    REQUIRE(reordered);
    EXPECT((near(reordered->at(80, 60), {0.0f, 1.0f, 0.0f})), "whatever order they are drawn in");
}

TEST_CASE("the back of a surface is lit too, a little darker")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    Surface back = square(60.0f, 0.0f, {1.0f, 1.0f, 1.0f});
    for (Point& n : back.normals) n.z = -1.0f;
    scene.surfaces.push_back(back);
    const auto picture = renderer()->draw(scene, 80, 60);
    REQUIRE(picture);
    const Colour seen = picture->at(40, 30);
    EXPECT((seen.red > 0.7f && seen.red < 0.95f), "lit from behind: 0.28 + 0.72 * 0.8");
}

TEST_CASE("lines are drawn over the background and pictures may change size")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    Scene scene = facing();
    scene.lines.push_back({{{-100.0f, 100.0f, 0.0f}, {100.0f, 100.0f, 0.0f}}, {1.0f, 1.0f, 0.0f}});
    const auto picture = renderer()->draw(scene, 101, 75);
    REQUIRE(picture);
    bool found = false;
    for (int y = 34; y <= 40; ++y) found = found || near(picture->at(50, y), {1.0f, 1.0f, 0.0f});
    EXPECT(found, "a line across the middle");
    const auto bigger = renderer()->draw(scene, 300, 200);
    EXPECT((bigger && bigger->width == 300 && bigger->height == 200), "a picture of another size");
}

TEST_CASE("the mannequin is in the picture of a session")
{
    if (!renderer()) SKIP("No Vulkan on this machine: " << whyNoVulkan());
    session::Session session({}, session::Threading::OneStepPerAdvance);
    session.update();
    OrbitCamera camera;
    const auto box = extentOf(session);
    REQUIRE(box);
    camera.frame(box->first, box->second);
    const Scene scene = sceneOf(session, camera, 1.0, {});
    const auto picture = renderer()->draw(scene, 120, 120);
    REQUIRE(picture);
    EXPECT((!near(picture->at(60, 60), scene.background)), "the body fills the middle");
    EXPECT((near(picture->at(2, 2), scene.background)), "with room around it");
}
