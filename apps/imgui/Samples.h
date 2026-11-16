#pragma once
// The example garments from the design, as documents the window can open.

#include <vector>

#include "clothcad/core/Document.h"

namespace clothcad::app {

struct Sample {
    const char* name;
    Document (*build)();
};

const std::vector<Sample>& samples();

} // namespace clothcad::app
