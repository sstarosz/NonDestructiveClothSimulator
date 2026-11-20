#include "clothcad/session/Lengths.h"

#include <cmath>

#include "clothcad/core/Fields.h"
#include "clothcad/core/Text.h"

namespace clothcad::session {

namespace {

// The formula that sets an edge's length, as text; empty when the length was
// typed in as a number, or when a later operation (a cut) has changed it.
std::string formulaSetting(const Edge& edge, double length, const Document& doc, const Result& shapes) {
    if (!doc.contains(edge.name.op)) return {};
    const OpData& operation = doc.get(edge.name.op);
    for (const Dimension& dimension : dimensions(operation, edge.name.op)) {
        if (dimension.edge.role != edge.name.role) continue;  // mirrored copies share the role
        const Expr formula = fields(operation)[dimension.field.index].value;
        if (asNumber(formula)) return {};
        const std::optional<double> value = valueOf(formula, shapes);
        if (!value || std::abs(*value - length) > 1e-6) return {};
        return formatExpr(formula, doc);
    }
    return {};
}

} // namespace

std::vector<EdgeLength> lengthsOf(const Document& doc, const Result& shapes, PanelId panel) {
    std::vector<EdgeLength> lengths;
    if (!doc.contains(panel)) return lengths;
    const Pattern& pattern = shapes[panel];
    for (const Edge& edge : pattern.edges) {
        if (edge.seam) continue;
        const double length = pattern.length(edge.name);
        lengths.push_back({edge.name, length, formulaSetting(edge, length, doc, shapes)});
    }
    return lengths;
}

} // namespace clothcad::session
