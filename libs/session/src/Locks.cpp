#include "clothcad/session/Locks.h"

#include "clothcad/core/Fields.h"

namespace clothcad::session {

namespace {

bool usesAFormula(const OpData& operation) {
    for (const Field& field : fields(operation))
        if (!asNumber(field.value)) return true;
    return false;
}

} // namespace

std::optional<OpRef<Mirror>> symmetryOf(const Document& doc, PanelId panel) {
    for (OpId op : doc.operations(panel))
        if (const Mirror* mirror = std::get_if<Mirror>(&doc.get(op)); mirror && mirror->appliedLast && doc.enabled(op))
            return OpRef<Mirror>(op);
    return std::nullopt;
}

DrawnCorner drawnSide(const Document& doc, PanelId panel, const CornerRef& corner) {
    const std::optional<OpRef<Mirror>> mirror = symmetryOf(doc, panel);
    if (!mirror || corner.via.empty() || !(corner.via.back() == OpId(*mirror))) return {corner, false};
    CornerRef original = corner;
    original.via.pop_back();
    return {original, true};
}

DrawnEdge drawnSide(const Document& doc, PanelId panel, const EdgeRef& edge) {
    const std::optional<OpRef<Mirror>> mirror = symmetryOf(doc, panel);
    if (!mirror || edge.via.empty() || !(edge.via.back() == OpId(*mirror))) return {edge, false};
    EdgeRef original = edge;
    original.via.pop_back();
    return {original, true};
}

std::string cornerLock(const Document& doc, const Pattern& shape, PanelId panel, const CornerRef& image) {
    const CornerRef corner = drawnSide(doc, panel, image).corner;
    // The operation that made the corner decides where it is.
    if (doc.contains(corner.op) && usesAFormula(doc.get(corner.op)))
        return "Locked: this corner's position comes from a formula. Change the formula in its layer.";
    // So does a length that a formula gives to an edge it ends.
    for (OpId op : doc.operations(panel)) {
        const SetLength* set = std::get_if<SetLength>(&doc.get(op));
        if (!set || !doc.enabled(op) || !usesAFormula(doc.get(op)) || !shape.contains(set->edge)) continue;
        const Edge& edge = shape.edge(set->edge);
        if (shape.vertices[edge.start].name == corner || shape.vertices[edge.end].name == corner)
            return "Locked: this corner is an end of an edge whose length comes from a formula. Change the formula in its layer.";
    }
    // So does any adjustment already made to it.
    for (PanelId anyPanel : doc.panels())
        for (OpId op : doc.operations(anyPanel))
            if (const AdjustPoint* adjust = std::get_if<AdjustPoint>(&doc.get(op)))
                if (adjust->corner == corner && usesAFormula(doc.get(op)))
                    return "Locked: this corner is moved by a formula. Change the formula in its layer.";
    return {};
}

std::optional<OpRef<Bezier>> curveOf(const Document& doc, PanelId panel, const EdgeRef& edge) {
    std::optional<OpRef<Bezier>> found;
    for (OpId op : doc.operations(panel))
        if (const Bezier* bezier = std::get_if<Bezier>(&doc.get(op)))
            if (bezier->edge == edge) found = OpRef<Bezier>(op);  // the last one wins, as it does when evaluated
    return found;
}

std::string handleLock(const Document& doc, PanelId panel, const EdgeRef& image, bool imageEndHandle) {
    // On the mirrored side of a symmetric panel, the handle stands for the
    // opposite handle of the original edge.
    const DrawnEdge drawn = drawnSide(doc, panel, image);
    const bool endHandle = drawn.wasImage ? !imageEndHandle : imageEndHandle;
    const std::optional<OpRef<Bezier>> curve = curveOf(doc, panel, drawn.edge);
    if (!curve) return "This curve is shaped in another panel, or by a mirror that is not applied last. Change it there.";
    const Offset& handle = endHandle ? doc.get(*curve).endHandle : doc.get(*curve).startHandle;
    if (!asNumber(handle.x) || !asNumber(handle.y))
        return "Locked: this handle comes from a formula. Change the formula in its layer.";
    return {};
}

} // namespace clothcad::session
