#pragma once
// The example garments, as documents a window can open; the same garments
// as examples/, built through the same verbs.
//
//   for (const Sample& sample : samples()) menu.add(sample.name, sample.build);

#include <vector>

#include "clothcad/core/Document.h"

namespace clothcad::session {

struct Sample {
    const char* name;
    Document (*build)();
};

const std::vector<Sample>& samples();

// Panels that all sit at the table's origin, as a sample or a document made
// in code leaves them, are laid side by side, 12 cm apart. A document whose
// panels have places of their own is left alone.
void layOutOnTheTable(Document& doc);

} // namespace clothcad::session
