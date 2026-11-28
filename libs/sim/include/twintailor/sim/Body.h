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
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "twintailor/core/Document.h"
#include "twintailor/core/Math.h"
#include "twintailor/core/Units.h"

namespace twintailor::sim {

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
// The numbers are read as the file has them; AvatarImport.h makes them cm, y
// up and facing +z. The surface should be closed, with its faces
// counter-clockwise seen from outside, as modelling programs export them.

struct LoadedSurface {
    Surface surface;
    std::string problem;  // why the file could not be used; empty if it could
};
// The vertices and faces of a Wavefront OBJ file's text. Faces with more
// than three corners are split into triangles.
LoadedSurface readObj(std::string_view text);
// The same, read from a file; the path is UTF-8.
LoadedSurface loadObj(const std::string& path);

// ---- The avatar of a document ----------------------------------------------
// A file's surface as the avatar a document holds (Document::setAvatar), under
// a name for people to read, turned, scaled and moved as `readAs` says
// (sim::imported) and keeping that; and that avatar as a surface again, as the
// document has it.
Avatar avatarOf(const Surface& file, std::string name, const AvatarImport& readAs = {});
Surface surfaceOf(const Avatar& avatar);

// How far making a body has got, for whoever waits for it, and a way to call
// it off. The one who makes it writes `done`; any thread may set `stop`.
//
//   sim::Progress progress;
//   auto made = std::async(std::launch::async, [&] { return sim::bodyFrom(surface, &progress); });
//   progress.done          // from 0 to 1
//   progress.stop = true;  // soon over, with a body that is nothing
struct Progress {
    std::atomic<float> done{0.0f};
    std::atomic<bool> stop{false};
};

// How far any point near a surface is from it, worked out once on a grid so
// that asking is quick. Exact within `band` of the surface; further away it
// only says which side the point is on. Working it out uses all the cores but
// one, and takes seconds for a surface of millions of triangles. Called off
// (Progress::stop), it holds nothing and every point is far from it.
class DistanceField {
public:
    explicit DistanceField(const Surface& surface, double cell = 1.0_cm, double band = 4.0_cm, Progress* progress = nullptr);
    Contact contact(Vec3 point) const;
    // How fast the distance it gives can change, at most, along any of the
    // grid's axes: cm per cm. About 1 for a clean closed surface, more where
    // surfaces cross or overlap and the distance jumps. A point that has
    // moved m cm has come at most m times the root of 3 times this nearer.
    double steepest() const { return steepest_; }

private:
    // The distances near the surface; false if it was called off.
    bool measure(const Surface& surface, Progress* progress);
    float at(int x, int y, int z) const { return distance_[(static_cast<std::size_t>(z) * ny_ + y) * nx_ + x]; }

    Vec3 origin_;
    double cell_ = 1.0, band_ = 4.0;
    double steepest_ = 0.0;
    int nx_ = 0, ny_ = 0, nz_ = 0;
    std::vector<float> distance_;
};

// A body that is the given surface, ready to collide with (see DistanceField).
Body bodyFrom(const Surface& surface, Progress* progress = nullptr);

} // namespace twintailor::sim
