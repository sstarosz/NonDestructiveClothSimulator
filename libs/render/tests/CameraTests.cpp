// Unit tests of Camera.cpp: where things land in the picture.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/render/Camera.h"

using namespace twintailor;
using namespace twintailor::render;

TEST_CASE("the camera looks at its target from the front, with y up")
{
    OrbitCamera camera;
    camera.pitch = 0.0;
    const Matrix m = camera.viewProjection(1.0);
    const Projected middle = project(m, camera.target);
    EXPECT((std::abs(middle.x) < 1e-5 && std::abs(middle.y) < 1e-5 && middle.depth > 0.0 && middle.depth < 1.0), "the target is in the middle of the picture");
    EXPECT((camera.eye().z > camera.target.z), "it starts in front of the body, which faces +z");
    const Projected right = project(m, camera.target + sim::Vec3{10.0, 0.0, 0.0});
    const Projected up = project(m, camera.target + sim::Vec3{0.0, 10.0, 0.0});
    EXPECT((right.x > 0.0 && up.y < 0.0), "+x is to the right and +y up (the picture's y runs down)");
    const Projected behind = project(m, camera.eye() + camera.toEye() * 10.0);
    EXPECT((behind.w <= 0.0), "what is behind the camera is said to be");
}

TEST_CASE("orbit, pan and dolly move the camera as Maya's does")
{
    OrbitCamera camera;
    const double distance = camera.distance;
    camera.orbit(300.0, 0.0);  // 90 degrees
    EXPECT((std::abs(camera.yaw + 90.0) < 1e-9 && std::abs(sim::length(camera.eye() - camera.target) - distance) < 1e-9), "orbiting keeps the distance");
    camera.orbit(0.0, 1000.0);
    EXPECT((camera.pitch <= 89.0), "it cannot turn over the top");

    OrbitCamera panned;
    const sim::Vec3 under = panned.target;
    panned.pan(100.0, 0.0, 800.0);
    const Projected now = project(panned.viewProjection(1.0), under);
    EXPECT((now.x > 0.0 && std::abs(now.y) < 1e-6), "panning left moves what was in the middle to the right, under the mouse");
    EXPECT((std::abs(now.x - 0.25) < 1e-3), "by as many pixels as the mouse: 100 of a picture 800 high is a quarter of half its height");

    camera.dolly(0.5);
    EXPECT((std::abs(camera.distance - distance / 2.0) < 1e-9), "dolly brings it nearer");
    camera.dolly(1e-9);
    EXPECT((camera.distance >= 10.0), "but not through its target");
}

TEST_CASE("framing a box puts all of it in the picture")
{
    OrbitCamera camera;
    camera.yaw = 30.0;
    const sim::Vec3 low{-40.0, 0.0, -25.0}, high{40.0, 175.0, 25.0};
    camera.frame(low, high);
    const Matrix m = camera.viewProjection(1.0);
    bool inside = true;
    for (int corner = 0; corner < 8; ++corner) {
        const sim::Vec3 p{corner & 1 ? high.x : low.x, corner & 2 ? high.y : low.y, corner & 4 ? high.z : low.z};
        const Projected at = project(m, p);
        inside = inside && at.w > 0.0 && std::abs(at.x) <= 1.0 && std::abs(at.y) <= 1.0 && at.depth > 0.0 && at.depth < 1.0;
    }
    EXPECT(inside, "every corner of the box is in view");
    EXPECT((std::abs(camera.target.y - 87.5) < 1e-9 && std::abs(camera.yaw - 30.0) < 1e-9), "about its middle, from where the camera looked");
}
