#pragma once
// A surface as its file has it, made the document's: in cm, y up, facing +z,
// standing on the floor. A file says nothing of its unit or axes, so they are
// guessed and then chosen by a person; what was done is kept with the avatar
// (core::AvatarImport) and can be undone for what is written back.
//
//   sim::LoadedSurface file = sim::loadObj("me.obj");
//   AvatarImport how = sim::guessImport(file.surface);     // metres, say
//   how.rescale = 1.1;                                     // a taller body than the file has
//   how.up = Axis::Z;                                      // a person knows better
//   how.facing = Axis::MinusY;
//   how = sim::standing(file.surface, how);                // feet on the floor
//   doc.setAvatar(std::make_shared<const Avatar>(sim::avatarOf(file.surface, "me.obj", how)));
//   sim::inFile(how, point)                                // a point of the document, as the file would have it

#include <array>
#include <string_view>

#include "twintailor/core/Document.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Vec3.h"

namespace twintailor::sim {

// The units a file can be in, with their lengths in cm.
struct FileUnit {
    std::string_view name;
    double cm;
};
inline constexpr std::array<FileUnit, 4> fileUnits = {{{"mm", 0.1}, {"cm", 1.0}, {"m", 100.0}, {"inch", 2.54}}};

// How large a surface's box is along each of its file's axes, in the file's
// units; zero for one with no points.
Vec3 sizeOf(const Surface& file);
// The size along an axis: of a box that size, how tall with that axis up.
double along(Vec3 size, Axis axis);
// The way a body with `up` is most likely to face: +z with y up, as most
// programs have it, and -y with z up, as Blender does.
Axis usualFacing(Axis up);

// What a file most likely is. Up: the longest side of its box, y and then z
// preferred to one barely longer (arms held out are as wide as a body is
// tall). Unit: the one of `fileUnits` that makes it as tall as a person, 120
// to 230 cm, as it is; a file that is that tall in none (many made-up models
// are about 1 tall) is taken in the unit that comes nearest and rescaled to
// 170 cm. Facing: usualFacing. Standing, as `standing` sets it.
AvatarImport guessImport(const Surface& file);

// `how` with `moved` set so that the surface stands on the floor (y = 0) in
// the middle of x and z. `how`'s up and facing must not lie along one line.
AvatarImport standing(const Surface& file, AvatarImport how);

// The file's surface in the document's terms: turned, scaled and moved as `how`
// says. Which side of a triangle faces out stays as it was.
Surface imported(const Surface& file, const AvatarImport& how);

// The other way round: a point of the document, and a direction in it (not
// scaled or moved), as the file has them.
Vec3 inFile(const AvatarImport& how, Vec3 point);
Vec3 directionInFile(const AvatarImport& how, Vec3 direction);

} // namespace twintailor::sim
