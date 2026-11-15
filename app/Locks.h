#pragma once
// What may be dragged on the canvas. Anything driven by a formula is locked:
// it is changed through its formula, not by dragging.

#include <optional>
#include <string>

#include "core/Document.h"

namespace clothcad::app {

// The panel's mirror that is applied last, if it has one that is switched
// on. Such a panel is symmetric: what is done to one side is done to both.
std::optional<OpRef<Mirror>> symmetryOf(const Document& doc, PanelId panel);

// On a symmetric panel, something on the mirrored side stands for its
// original on the drawn side. These give the original and say whether the
// name was a mirror image.
struct DrawnCorner {
    CornerRef corner;
    bool wasImage;
};
struct DrawnEdge {
    EdgeRef edge;
    bool wasImage;  // if so the edge runs the other way: its start is the image's end
};
DrawnCorner drawnSide(const Document& doc, PanelId panel, const CornerRef& corner);
DrawnEdge drawnSide(const Document& doc, PanelId panel, const EdgeRef& edge);

// Why a corner can't be dragged; empty if it can.
std::string cornerLock(const Document& doc, PanelId panel, const CornerRef& corner);

// The free-curve operation in `panel` whose handles shape `edge`, if the
// panel has one. A curve that comes from another panel (through an instance)
// or from the other side of a mirror has none here.
std::optional<OpRef<Bezier>> curveOf(const Document& doc, PanelId panel, const EdgeRef& edge);

// Why a free curve's handle can't be dragged; empty if it can.
std::string handleLock(const Document& doc, PanelId panel, const EdgeRef& edge, bool endHandle);

} // namespace clothcad::app
