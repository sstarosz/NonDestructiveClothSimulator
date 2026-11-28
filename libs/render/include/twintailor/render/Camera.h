#pragma once
// A camera that turns about a point, as Maya's does: orbit, pan (track) and
// dolly, and frame a box. In cm and degrees; y is up and the body faces +z,
// so the camera starts in front of it.
//
//   render::OrbitCamera camera;
//   camera.frame({-30, 0, -20}, {30, 180, 20});
//   camera.orbit(dx, dy);                 // pixels the mouse moved
//   scene.viewProjection = camera.viewProjection(width / double(height));

#include "twintailor/core/Math.h"
#include "twintailor/render/Scene.h"

namespace twintailor::render {

class OrbitCamera {
public:
    Vec3 target{0.0, 100.0, 0.0};  // what it turns about
    double yaw = 0.0;        // degrees about the up axis; 0 looks at the front (from +z)
    double pitch = 10.0;     // degrees up from level; positive looks down on the target
    double distance = 400.0; // cm from the target
    double fieldOfView = 35.0;  // degrees, top to bottom

    void orbit(double dxPixels, double dyPixels);
    // Moves the target with the mouse, so that what is under it stays under it.
    void pan(double dxPixels, double dyPixels, double viewHeightPixels);
    // Nearer (factor below 1) or farther (above 1).
    void dolly(double factor);
    // Everything in the box in view, seen from where the camera looks now.
    void frame(Vec3 low, Vec3 high);

    Vec3 eye() const;
    Vec3 toEye() const;  // the unit direction from the target to the eye
    Matrix viewProjection(double aspect) const;
};

// The product a * b of two matrices.
Matrix operator*(const Matrix& a, const Matrix& b);
// Where a point in space lands in the picture: x and y from -1 to 1 (y down),
// depth from 0 (near) to 1 (far). w <= 0 means behind the camera.
struct Projected {
    double x, y, depth, w;
};
Projected project(const Matrix& viewProjection, Vec3 point);

} // namespace twintailor::render
