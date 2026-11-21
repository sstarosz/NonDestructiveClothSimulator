#pragma once
// The project's avatar (assets/avatarV1.obj), for the integration tests and
// benchmarks that drape cloth on it; the unit tests read no files. The build
// says where the project's assets are (CLOTHCAD_ASSETS_DIR).
//
//   const sim::Body& body = project::avatar();
//   window.loadAvatar(QString::fromStdString(project::avatarPath()));
//
// It stands 158 cm tall, with its arms held away from its sides; its hips
// (about 98 cm around) are widest at y = 78 and its waist (about 65 cm
// around) is narrowest at y = 102.

#include <string>

#include "clothcad/sim/Body.h"

namespace clothcad::project {

inline std::string avatarPath() { return std::string(CLOTHCAD_ASSETS_DIR) + "/avatarV1.obj"; }

// Read and made ready to collide with once in a test program: that takes a moment.
inline const sim::Body& avatar() {
    static const sim::Body body = sim::bodyFrom(sim::loadObj(avatarPath()).surface);
    return body;
}

// Where its hips are widest and its waist is narrowest, in cm above the floor.
inline constexpr double avatarHips = 78.0;
inline constexpr double avatarWaist = 102.0;

} // namespace clothcad::project
