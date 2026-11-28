#pragma once
// Private to the library: how a drag of corners and handles comes to the drawn
// side of a panel, and other questions that the commands and Drafting share.

#include <expected>
#include <string>
#include <vector>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Pattern.h"

namespace twintailor::session {

struct Movement {
    double dx, dy;
};

// On a symmetric panel a drag has to respect the mirror: a drag on the
// mirrored side is the mirror image of the drag on the drawn side, and a
// point lying on the mirror's axis (`position`) may only slide along it.
Movement acrossTheMirror(const Document& doc, const Result& shapes, PanelId panel, Movement moved, bool wasImage, const Vec2* position);

struct MovingCorner {
    CornerRef corner;  // the drawn original that moves
    Movement by;       // in the panel's own coordinates
};

// The corners that move when `corners` are dragged by `own` (in the panel's own
// coordinates): their drawn originals, each once, without those a formula
// places. With none left, why (empty if there was no such corner).
std::expected<std::vector<MovingCorner>, std::string> movableCorners(const Document& doc, const Result& shapes, PanelId panel,
                                                                     const std::vector<CornerRef>& corners, Movement own);

// Whether the document has a panel of this name.
bool panelNamed(const Document& doc, const std::string& name);

// The layers that give the drawn edge `drawn` a length of its own, but those that are off.
std::vector<OpId> ownLengthsOf(const Document& doc, PanelId panel, const EdgeRef& drawn);

} // namespace twintailor::session
