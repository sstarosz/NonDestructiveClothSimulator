#include "twintailor/session/Lengths.h"

#include <cmath>

#include "twintailor/core/Fields.h"
#include "twintailor/core/Text.h"
#include "twintailor/session/Locks.h"

namespace twintailor::session {

namespace {

// The formula that sets an edge's length, as text; empty when the length was
// typed in as a number, or when a later operation (a cut) has changed it.
std::string formulaSetting(const EdgeRef& edge, const Document& doc, const Result& shapes, PanelId panel) {
    const std::optional<LengthSetting> setting = lengthSetBy(doc, shapes, panel, edge);
    if (!setting) return {};
    const Expr formula = fields(doc.get(setting->operation))[setting->field.index].value;
    return asNumber(formula) ? std::string{} : formatExpr(formula, doc);
}

} // namespace

std::optional<LengthSetting> lengthSetBy(const Document& doc, const Result& shapes, PanelId panel, const EdgeRef& edge) {
    if (!doc.contains(panel)) return std::nullopt;
    const Pattern& shape = shapes[panel];
    if (!shape.contains(edge)) return std::nullopt;
    const double length = shape.length(edge);
    const auto gives = [&](const Expr& value) {
        const std::optional<double> number = valueOf(value, shapes);
        return number && std::abs(*number - length) <= 1e-6;
    };

    // A SetLength is made for the drawn edge; on a symmetric panel the mirrored side has the same length.
    const EdgeRef drawn = drawnSide(doc, panel, edge).edge;
    std::optional<OpId> last;
    for (OpId op : doc.operations(panel))
        if (doc.enabled(op))
            if (const SetLength* set = std::get_if<SetLength>(&doc.get(op)); set && set->edge == drawn) last = op;
    if (last && gives(std::get<SetLength>(doc.get(*last)).length)) return LengthSetting{*last, FieldId{0}};

    if (!doc.contains(edge.op)) return std::nullopt;
    const OpData& operation = doc.get(edge.op);
    for (const Dimension& dimension : dimensions(operation, edge.op)) {
        if (dimension.edge.role != edge.role) continue;  // mirrored copies share the role
        if (gives(fields(operation)[dimension.field.index].value)) return LengthSetting{edge.op, dimension.field};
    }
    return std::nullopt;
}

std::vector<EdgeLength> lengthsOf(const Document& doc, const Result& shapes, PanelId panel) {
    std::vector<EdgeLength> lengths;
    if (!doc.contains(panel)) return lengths;
    const Pattern& pattern = shapes[panel];
    for (const Edge& edge : pattern.edges) {
        if (edge.seam) continue;
        const double length = pattern.length(edge.name);
        lengths.push_back({edge.name, length, formulaSetting(edge.name, doc, shapes, panel)});
    }
    return lengths;
}

} // namespace twintailor::session
