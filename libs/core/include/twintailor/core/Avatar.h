#pragma once
// The body a garment is made on, and how its file was read. The document
// holds it (Document::setAvatar).
//
//   doc.setAvatar(std::make_shared<const Avatar>(Avatar{.name = "me.obj", .points = points, .triangles = triangles}));

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "twintailor/core/Math.h"

namespace twintailor {

// A direction along one of the axes of a file.
enum class Axis { X, Y, Z, MinusX, MinusY, MinusZ };

// Whether two directions lie along the same axis, one way or the other.
constexpr bool alongOneLine(Axis a, Axis b) { return static_cast<int>(a) % 3 == static_cast<int>(b) % 3; }

// How an avatar's file was read: its points were turned so that `up` became y
// and `facing` became +z (the two are never along one line), taken to be in
// `unit`, made `rescale` times as large as that, and then shifted by `moved`.
// What it is by default: the file was in cm already, y up, facing +z, where it
// should stand. Kept so that what is written for other programs can be given
// back as the file had it.
struct AvatarImport {
    double unit = 1.0;     // cm in one unit of the file; more than 0
    double rescale = 1.0;  // how many times as large as the file has it the body was made; more than 0
    Axis up = Axis::Y;
    Axis facing = Axis::Z;
    Vec3 moved{};  // in cm
    // What one unit of the file came to be, in cm.
    double cmPerUnit() const { return unit * rescale; }
    friend bool operator==(const AvatarImport&, const AvatarImport&) = default;
};

// The body the garment is made on: its surface as triangles, standing on the
// floor and facing +z, counter-clockwise seen from outside. It is part of the
// document and saved in its file, so that one file holds the whole scene.
struct Avatar {
    std::string name;  // display label: the file it was read from
    std::vector<Vec3> points;  // in cm, y up, whatever the file had
    std::vector<std::array<std::uint32_t, 3>> triangles;  // each names three of the points
    AvatarImport readAs{};
    friend bool operator==(const Avatar&, const Avatar&) = default;
};

} // namespace twintailor
