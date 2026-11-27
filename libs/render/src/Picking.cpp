#include "clothcad/render/Picking.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace clothcad::render {

namespace {

// The camera's directions, as its view matrix has them.
struct Frame {
    sim::Vec3 forward, side, upright;
};
Frame frameOf(const OrbitCamera& camera) {
    const sim::Vec3 forward = camera.toEye() * -1.0;
    const sim::Vec3 side = sim::normalized(sim::cross(forward, {0.0, 1.0, 0.0}));
    return {forward, side, sim::cross(side, forward)};
}

// The picture's focal length in its pixels: what one cm sideways at one cm
// ahead spans, as the projection has it.
double focalOf(const OrbitCamera& camera, ViewSize size) {
    return std::max(size.height, 1) / 2.0 / std::tan(camera.fieldOfView * std::numbers::pi / 180.0 / 2.0);
}

// The nearest triangle of the scene's surfaces the ray meets (Möller and Trumbore).
// A see-through body is passed through: what shows behind it can be clicked.
struct Met {
    std::size_t surface;
    std::array<std::uint32_t, 3> corners;
    double distance;
};
std::optional<Met> nearestMet(const Scene& scene, const Ray& ray) {
    std::optional<Met> nearest;
    for (std::size_t s = 0; s < scene.surfaces.size(); ++s) {
        const Surface& surface = scene.surfaces[s];
        if (surface.nearestOnly && !surface.panel) continue;
        const std::vector<Point>& points = surface.shownPoints();
        const std::vector<std::uint32_t>& triangles = surface.shownTriangles();
        const auto at = [&](std::uint32_t i) { return sim::Vec3{points[i].x, points[i].y, points[i].z}; };
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            const std::array<std::uint32_t, 3> corners{triangles[t], triangles[t + 1], triangles[t + 2]};
            if (corners[0] >= points.size() || corners[1] >= points.size() || corners[2] >= points.size()) continue;
            const sim::Vec3 a = at(corners[0]), ab = at(corners[1]) - a, ac = at(corners[2]) - a;
            const sim::Vec3 p = sim::cross(ray.direction, ac);
            const double det = sim::dot(ab, p);
            if (std::abs(det) < 1e-12) continue;
            const sim::Vec3 fromA = ray.from - a;
            const double u = sim::dot(fromA, p) / det;
            if (u < 0.0 || u > 1.0) continue;
            const sim::Vec3 q = sim::cross(fromA, ab);
            const double v = sim::dot(ray.direction, q) / det;
            if (v < 0.0 || u + v > 1.0) continue;
            const double distance = sim::dot(ac, q) / det;
            if (distance > 0.0 && (!nearest || distance < nearest->distance)) nearest = Met{s, corners, distance};
        }
    }
    return nearest;
}

// Where the ray meets the plane through `point` facing along `normal`.
std::optional<sim::Vec3> onPlane(const Ray& ray, sim::Vec3 point, sim::Vec3 normal) {
    const double facing = sim::dot(ray.direction, normal);
    if (std::abs(facing) < 1e-9) return std::nullopt;
    return ray.from + ray.direction * (sim::dot(point - ray.from, normal) / facing);
}

} // namespace

Ray rayThrough(const OrbitCamera& camera, ViewSize size, PicturePoint at) {
    const Frame frame = frameOf(camera);
    const double focal = focalOf(camera, size);
    return {camera.eye(), sim::normalized(frame.forward * focal + frame.side * (at.x - size.width / 2.0) - frame.upright * (at.y - size.height / 2.0))};
}

std::optional<PicturePoint> pictureOf(const OrbitCamera& camera, ViewSize size, sim::Vec3 point) {
    const Frame frame = frameOf(camera);
    const sim::Vec3 fromEye = point - camera.eye();
    const double ahead = sim::dot(fromEye, frame.forward);
    if (ahead <= 1e-9) return std::nullopt;
    const double focal = focalOf(camera, size);
    return PicturePoint{size.width / 2.0 + focal * sim::dot(fromEye, frame.side) / ahead, size.height / 2.0 - focal * sim::dot(fromEye, frame.upright) / ahead};
}

std::optional<PanelId> panelAt(const Scene& scene, const OrbitCamera& camera, ViewSize size, PicturePoint at) {
    const std::optional<Met> met = nearestMet(scene, rayThrough(camera, size, at));
    if (!met) return std::nullopt;
    return scene.surfaces[met->surface].panel;
}

std::optional<ClothHit> clothAt(const Scene& scene, const OrbitCamera& camera, ViewSize size, PicturePoint at) {
    const Ray ray = rayThrough(camera, size, at);
    const std::optional<Met> met = nearestMet(scene, ray);
    if (!met || !scene.surfaces[met->surface].panel) return std::nullopt;  // nothing, or the body in front
    const Surface& surface = scene.surfaces[met->surface];
    const sim::Vec3 spot = ray.from + ray.direction * met->distance;
    std::uint32_t nearest = met->corners[0];
    double best = 1e300;
    for (const std::uint32_t corner : met->corners) {
        const Point& point = surface.shownPoints()[corner];
        const sim::Vec3 there{point.x, point.y, point.z};
        if (const double away = sim::length(there - spot); away < best) best = away, nearest = corner;
    }
    return ClothHit{*surface.panel, nearest, spot};
}

std::optional<sim::Vec3> acrossTheView(const OrbitCamera& camera, ViewSize size, sim::Vec3 held, PicturePoint pressed, PicturePoint now) {
    const sim::Vec3 facing = camera.toEye();
    const std::optional<sim::Vec3> from = onPlane(rayThrough(camera, size, pressed), held, facing);
    const std::optional<sim::Vec3> to = onPlane(rayThrough(camera, size, now), held, facing);
    if (!from || !to) return std::nullopt;
    return *to - *from;
}

} // namespace clothcad::render
