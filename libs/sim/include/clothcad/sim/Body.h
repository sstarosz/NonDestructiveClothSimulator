#pragma once
// The body the cloth is put on: an avatar's surface read from a file, made
// ready to collide with. Rounded limbs (Capsule) can be added too: exact
// and fast, as a ball for tests.
//
//   sim::LoadedSurface avatar = sim::loadObj("assets/avatarV1.obj");
//   sim::Body body = sim::bodyFrom(avatar.surface);
//   sim::distanceTo(body, point)    // negative inside the body
//
// The body stands on the floor (y = 0) at x = 0, z = 0, facing +z.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "clothcad/core/Units.h"
#include "clothcad/sim/Vec3.h"

namespace clothcad::sim {

// A limb: a ball of radiusA at a, a ball of radiusB at b, and everything
// between them.
struct Capsule {
    Vec3 a, b;
    double radiusA = 0.0, radiusB = 0.0;
};

class DistanceField;

// What the cloth rests on: limbs, a scanned surface, or both; nothing: no body.
struct Body {
    std::vector<Capsule> parts;
    std::shared_ptr<const DistanceField> scanned;  // a surface loaded from a file, if any
};

// How far a point is from the body's surface; negative inside. Large and
// positive for a body with no parts.
double distanceTo(const Body& body, Vec3 point);

// The nearest limb's surface as seen from a point: how far away it is
// (negative inside) and which way is out.
struct Contact {
    double distance = 1e30;
    Vec3 outward{0.0, 1.0, 0.0};
};
Contact contactWith(const Capsule& limb, Vec3 point);

// The body's limbs as triangles, counter-clockwise seen from outside.
// The parts overlap; each is closed on its own.
struct Surface {
    std::vector<Vec3> points;
    std::vector<std::array<uint32_t, 3>> triangles;
};
Surface surfaceOf(const Body& body, int around = 14);

// ---- A body from a file ----------------------------------------------------
//
//   sim::LoadedSurface avatar = sim::loadObj("assets/avatarV1.obj");
//   if (avatar.problem.empty()) body = sim::bodyFrom(avatar.surface);
//
// The file's units are taken to be cm, with y up, the feet on y = 0 and the
// body facing +z. The surface should be closed, with its faces
// counter-clockwise seen from outside, as modelling programs export them.

struct LoadedSurface {
    Surface surface;
    std::string problem;  // why the file could not be used; empty if it could
};
// The vertices and faces of a Wavefront OBJ file's text. Faces with more
// than three corners are split into triangles.
LoadedSurface readObj(std::string_view text);
// The same, read from a file.
LoadedSurface loadObj(const std::string& path);

// How far any point near a surface is from it, worked out once on a grid so
// that asking is quick. Exact within `band` of the surface; further away it
// only says which side the point is on.
class DistanceField {
public:
    explicit DistanceField(const Surface& surface, double cell = 1.0_cm, double band = 4.0_cm);
    Contact contact(Vec3 point) const;

private:
    float at(int x, int y, int z) const { return distance_[(static_cast<std::size_t>(z) * ny_ + y) * nx_ + x]; }

    Vec3 origin_;
    double cell_ = 1.0, band_ = 4.0;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<float> distance_;
};

// A body that is the given surface.
Body bodyFrom(const Surface& surface);

} // namespace clothcad::sim
