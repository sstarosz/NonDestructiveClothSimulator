#include "twintailor/render/Camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace twintailor::render {

namespace {

double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

constexpr double nearest = 10.0, farthest = 20000.0;  // cm the camera may come to, or go from, its target

Matrix lookAt(sim::Vec3 eye, sim::Vec3 target, sim::Vec3 up) {
    const sim::Vec3 forward = sim::normalized(target - eye);
    const sim::Vec3 side = sim::normalized(sim::cross(forward, up));
    const sim::Vec3 upright = sim::cross(side, forward);
    Matrix m;
    const sim::Vec3 rows[3] = {side, upright, forward * -1.0};
    for (int row = 0; row < 3; ++row) {
        m.at(row, 0) = static_cast<float>(rows[row].x);
        m.at(row, 1) = static_cast<float>(rows[row].y);
        m.at(row, 2) = static_cast<float>(rows[row].z);
        m.at(row, 3) = static_cast<float>(-sim::dot(rows[row], eye));
    }
    m.at(3, 3) = 1.0f;
    return m;
}

// Vulkan's picture: y runs down and depth from 0 to 1.
Matrix perspective(double fieldOfView, double aspect, double nearPlane, double farPlane) {
    const double focal = 1.0 / std::tan(radians(fieldOfView) / 2.0);
    Matrix m;
    m.at(0, 0) = static_cast<float>(focal / aspect);
    m.at(1, 1) = static_cast<float>(-focal);
    m.at(2, 2) = static_cast<float>(farPlane / (nearPlane - farPlane));
    m.at(2, 3) = static_cast<float>(nearPlane * farPlane / (nearPlane - farPlane));
    m.at(3, 2) = -1.0f;
    return m;
}

} // namespace

Matrix operator*(const Matrix& a, const Matrix& b) {
    Matrix m;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, column);
            m.at(row, column) = sum;
        }
    return m;
}

Projected project(const Matrix& m, sim::Vec3 p) {
    double out[4];
    for (int row = 0; row < 4; ++row) out[row] = m.at(row, 0) * p.x + m.at(row, 1) * p.y + m.at(row, 2) * p.z + m.at(row, 3);
    if (out[3] <= 0.0) return {0.0, 0.0, 0.0, out[3]};
    return {out[0] / out[3], out[1] / out[3], out[2] / out[3], out[3]};
}

void OrbitCamera::orbit(double dxPixels, double dyPixels) {
    yaw = std::remainder(yaw - dxPixels * 0.3, 360.0);
    pitch = std::clamp(pitch + dyPixels * 0.3, -89.0, 89.0);
}

void OrbitCamera::pan(double dxPixels, double dyPixels, double viewHeightPixels) {
    const double perPixel = 2.0 * distance * std::tan(radians(fieldOfView) / 2.0) / std::max(viewHeightPixels, 1.0);
    const sim::Vec3 back = toEye();
    const sim::Vec3 side = sim::normalized(sim::cross({0.0, 1.0, 0.0}, back));
    const sim::Vec3 upright = sim::cross(back, side);
    target = target - side * (dxPixels * perPixel) + upright * (dyPixels * perPixel);
}

void OrbitCamera::dolly(double factor) { distance = std::clamp(distance * factor, nearest, farthest); }

void OrbitCamera::frame(sim::Vec3 low, sim::Vec3 high) {
    target = (low + high) * 0.5;
    const double radius = std::max(sim::length(high - low) / 2.0, 1.0);
    distance = std::clamp(radius / std::sin(radians(fieldOfView) / 2.0) * 1.05, nearest, farthest);
}

sim::Vec3 OrbitCamera::toEye() const {
    const double up = radians(pitch), around = radians(yaw);
    return {std::cos(up) * std::sin(around), std::sin(up), std::cos(up) * std::cos(around)};
}

sim::Vec3 OrbitCamera::eye() const { return target + toEye() * distance; }

Matrix OrbitCamera::viewProjection(double aspect) const {
    // Near enough for a close look, far enough for the floor around the body.
    const double nearPlane = std::max(distance * 0.02, 0.5), farPlane = distance * 4.0 + 2000.0;
    return perspective(fieldOfView, std::max(aspect, 0.01), nearPlane, farPlane) * lookAt(eye(), target, {0.0, 1.0, 0.0});
}

} // namespace twintailor::render
