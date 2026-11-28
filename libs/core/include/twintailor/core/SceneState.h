#pragma once
// How the scene was left: the draped cloth, and what was hidden or frozen. It
// is saved in the file beside the pattern (File.h), but is no part of the
// document.
//
//   SceneState state{.hidden = {sleeve}};
//   save(doc, "skirt.twintail", state);

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "twintailor/core/Ids.h"
#include "twintailor/core/Math.h"

namespace twintailor {

// The cloth of one panel as it hung: its particles, each where it is in the
// panel's own flat coordinates and where it is in space, and the triangles
// between them. The flat coordinates let a drape be carried over to a mesh
// that is not made quite the same.
struct DrapedPanel {
    PanelId panel;
    Vec2 anchor;                                          // the panel's anchor when the mesh was made
    std::vector<Vec2> flat;                               // per particle
    std::vector<Vec3> at;                               // per particle, as many as flat
    std::vector<std::array<std::uint32_t, 3>> triangles;  // each names three of the particles
    friend bool operator==(const DrapedPanel&, const DrapedPanel&) = default;
};

struct SavedDrape {
    double time = 0.0;    // seconds of cloth time
    bool sewing = false;  // its seams were still closing
    std::vector<DrapedPanel> panels;
    friend bool operator==(const SavedDrape&, const SavedDrape&) = default;
};

// How the scene was left. Saved in the file beside the pattern, but no part of
// the document: changing it is no undo step.
struct SceneState {
    std::vector<PanelId> frozen;  // panels deactivated: they take no part in the cloth
    std::vector<PanelId> hidden;  // panels not drawn in space
    std::vector<SeamId> hiddenSeams;
    bool bodyHidden = false;  // and so out of the simulation
    bool bodySeeThrough = false;
    std::optional<SavedDrape> drape;  // none: the panels stand as they are arranged
    friend bool operator==(const SceneState&, const SceneState&) = default;
};

} // namespace twintailor
