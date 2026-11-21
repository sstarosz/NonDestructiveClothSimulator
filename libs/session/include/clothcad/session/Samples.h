#pragma once
// The example garments, as documents a window can open, each with the body
// it is draped on; the same garments as examples/, built through the same
// verbs.
//
//   for (const Sample& sample : samples()) menu.add(sample.name, sample.build);

#include <vector>

#include "clothcad/core/Document.h"

namespace clothcad::session {

struct Sample {
    const char* name;
    Document (*build)();
    // The avatar it is draped on, a Wavefront OBJ file in the project's
    // assets/ folder (the window finds it beside the program).
    const char* avatar;
};

const std::vector<Sample>& samples();

// Panels that all sit at the table's origin, as a sample or a document made
// in code leaves them, are laid side by side, 12 cm apart. A document whose
// panels have places of their own is left alone.
void layOutOnTheTable(Document& doc);

} // namespace clothcad::session
