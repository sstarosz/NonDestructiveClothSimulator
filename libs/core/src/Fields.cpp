#include "twintailor/core/Fields.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

#include "twintailor/core/Document.h"
#include "internal/FieldAccess.h"

namespace twintailor {

namespace {

// One overload per operation type: its label, and each formula field in the
// order the window shows them, with the values it accepts. This table is the
// one place those rules are written. Leaving a type out is a compile error.
// `visit(label, field, limits, unit)` is called once per field.
//
// Evaluate.cpp reads the fields' values in this same order.
template <class Visit> std::string_view describe(Rect& op, Visit visit) {
    visit("Width", op.width, Limits::moreThan(0.0), Unit::Length);
    visit("Height", op.height, Limits::moreThan(0.0), Unit::Length);
    return "Rectangle";
}
template <class Visit> std::string_view describe(Circle& op, Visit visit) {
    visit("Radius", op.radius, Limits::moreThan(0.0), Unit::Length);
    visit("Sweep", op.sweep, Limits::moreThanUpTo(0.0, 360.0), Unit::Angle);
    return "Circle";
}
template <class Visit> std::string_view describe(RingSector& op, Visit visit) {
    visit("Inner length", op.innerLength, Limits::moreThan(0.0), Unit::Length);
    visit("Sweep", op.sweep, Limits::moreThanUpTo(0.0, 360.0), Unit::Angle);
    visit("Depth", op.depth, Limits::moreThan(0.0), Unit::Length);
    return "Ring sector";
}
template <class Visit> std::string_view describe(Instance&, Visit) { return "Instance"; }
template <class Visit> std::string_view describe(Curve& op, Visit visit) {
    visit("Bulge", op.bulge, Limits::any(), Unit::Length);
    return "Curve";
}
template <class Visit> std::string_view describe(Bezier& op, Visit visit) {
    visit("Start handle x", op.startHandle.x, Limits::any(), Unit::Length);
    visit("Start handle y", op.startHandle.y, Limits::any(), Unit::Length);
    visit("End handle x", op.endHandle.x, Limits::any(), Unit::Length);
    visit("End handle y", op.endHandle.y, Limits::any(), Unit::Length);
    return "Free curve";
}
template <class Visit> std::string_view describe(Line& op, Visit visit) {
    visit("Angle", op.angle, Limits::any(), Unit::Angle);
    return "Angled line";
}
template <class Visit> std::string_view describe(LineBetween& op, Visit visit) {
    visit("From position", op.from.along, Limits::between(0.0, 1.0), Unit::Fraction);
    visit("To position", op.to.along, Limits::between(0.0, 1.0), Unit::Fraction);
    return "Line";
}
template <class Visit> std::string_view describe(Offcut&, Visit) { return "Offcut"; }
template <class Visit> std::string_view describe(Cut&, Visit) { return "Cut"; }
template <class Visit> std::string_view describe(Mirror&, Visit) { return "Mirror"; }
template <class Visit> std::string_view describe(AdjustPoint& op, Visit visit) {
    visit("Move x", op.dx, Limits::any(), Unit::Length);
    visit("Move y", op.dy, Limits::any(), Unit::Length);
    return "Adjust point";
}
template <class Visit> std::string_view describe(AddPoint& op, Visit visit) {
    // Not at the very ends: a point there would be a corner already present.
    visit("Position", op.at.along, Limits::strictlyBetween(0.0, 1.0), Unit::Fraction);
    return "Add point";
}
template <class Visit> std::string_view describe(Scale& op, Visit visit) {
    visit("Scale x", op.x, Limits::moreThan(0.0), Unit::Fraction);
    visit("Scale y", op.y, Limits::moreThan(0.0), Unit::Fraction);
    return "Scale";
}
template <class Visit> std::string_view describe(SetLength& op, Visit visit) {
    visit("Length", op.length, Limits::moreThan(0.0), Unit::Length);
    return "Set length";
}

// One overload per operation type again: each reference it holds, in the
// order the window shows them. `visit(label, reference)` is called per slot.
template <class Visit> void describeReferences(Rect&, Visit) {}
template <class Visit> void describeReferences(Circle&, Visit) {}
template <class Visit> void describeReferences(RingSector&, Visit) {}
template <class Visit> void describeReferences(Instance& op, Visit visit) { visit("Source panel", op.source); }
template <class Visit> void describeReferences(Curve& op, Visit visit) { visit("Edge", op.edge); }
template <class Visit> void describeReferences(Bezier& op, Visit visit) { visit("Edge", op.edge); }
template <class Visit> void describeReferences(Line& op, Visit visit) { visit("From corner", op.from); }
template <class Visit> void describeReferences(LineBetween& op, Visit visit) {
    visit("From edge", op.from.edge);
    visit("To edge", op.to.edge);
}
template <class Visit> void describeReferences(Offcut&, Visit) {}
template <class Visit> void describeReferences(Cut& op, Visit visit) { visit("Along line", op.along); }
template <class Visit> void describeReferences(Mirror& op, Visit visit) { visit("Across edge", op.across); }
template <class Visit> void describeReferences(AdjustPoint& op, Visit visit) { visit("Corner", op.corner); }
template <class Visit> void describeReferences(AddPoint& op, Visit visit) { visit("Edge", op.at.edge); }
template <class Visit> void describeReferences(Scale&, Visit) {}
template <class Visit> void describeReferences(SetLength& op, Visit visit) { visit("Edge", op.edge); }

// What each operation type calls the edges and corners it creates, in the
// order of its Edge and Corner enums.
std::string_view edgeRoleName(const OpData& operation, uint16_t role) {
    static constexpr std::string_view rect[] = {"bottom edge", "right edge", "top edge", "left edge"};
    static constexpr std::string_view ring[] = {"inner edge", "outer edge", "start side", "end side"};
    static constexpr std::string_view circle[] = {"arc", "start side", "end side"};
    if (std::holds_alternative<Rect>(operation) && role < 4) return rect[role];
    if (std::holds_alternative<RingSector>(operation) && role < 4) return ring[role];
    if (std::holds_alternative<Circle>(operation) && role < 3) return circle[role];
    if (std::holds_alternative<Cut>(operation)) return "cut edge";
    return "edge";
}

std::string_view cornerRoleName(const OpData& operation, uint16_t role) {
    static constexpr std::string_view rect[] = {"bottom-left corner", "bottom-right corner", "top-right corner", "top-left corner"};
    static constexpr std::string_view ring[] = {"inner start corner", "inner end corner", "outer start corner", "outer end corner"};
    static constexpr std::string_view cut[] = {"cut start corner", "cut end corner"};
    if (std::holds_alternative<Rect>(operation) && role < 4) return rect[role];
    if (std::holds_alternative<RingSector>(operation) && role < 4) return ring[role];
    static constexpr std::string_view circle[] = {"start corner", "end corner", "centre"};
    if (std::holds_alternative<Circle>(operation) && role < 3) return circle[role];
    if (std::holds_alternative<Cut>(operation) && role < 2) return cut[role];
    if (std::holds_alternative<AddPoint>(operation)) return "added point";
    return "corner";
}

// "2. Curve": the operation's place in its panel and its label.
std::string numbered(OpId operation, const Document& doc) {
    const std::vector<OpId> siblings = doc.operations(doc.panelOf(operation));
    std::size_t number = 1;
    for (OpId sibling : siblings) {
        if (sibling == operation) break;
        ++number;
    }
    return std::to_string(number) + ". " + std::string(label(doc.get(operation)));
}

std::string missing(OpId operation) { return operation == OpId{} ? "nothing chosen" : "something that was removed"; }

// What a name's `via` says about it: each mirror it passed through, and each
// cut that left it as the far piece of its edge.
std::string viaWords(const std::vector<OpId>& via, const Document& doc) {
    std::string words;
    for (OpId op : via) {
        if (doc.contains(op) && std::holds_alternative<Cut>(doc.get(op))) words += "far part of ";
        else if (doc.contains(op) && std::holds_alternative<AddPoint>(doc.get(op))) words += "part after the point of ";
        else words += "mirrored ";
    }
    return words;
}

} // namespace

Limits Limits::any() {
    return {-std::numeric_limits<double>::infinity(), false, std::numeric_limits<double>::infinity(), false};
}
Limits Limits::moreThan(double low) { return {low, false, std::numeric_limits<double>::infinity(), false}; }
Limits Limits::moreThanUpTo(double low, double high) { return {low, false, high, true}; }
Limits Limits::between(double low, double high) { return {low, true, high, true}; }
Limits Limits::strictlyBetween(double low, double high) { return {low, false, high, false}; }

bool Limits::allows(double value) const {
    if (!(value == value)) return false;  // not a number
    const double slack = 1e-9;            // so that 360 computed as 359.9999999999 or 360.0000000001 still passes
    if (lowIncluded ? value < low - slack : value <= low) return false;
    if (highIncluded ? value > high + slack : value >= high) return false;
    return true;
}

std::string Limits::rule() const {
    const auto number = [](double v) {
        char text[32];
        std::snprintf(text, sizeof text, "%g", v);
        return std::string(text);
    };
    const bool hasLow = std::isfinite(low), hasHigh = std::isfinite(high);
    std::string text;
    if (hasLow) text = std::string(lowIncluded ? "must be at least " : "must be greater than ") + number(low);
    if (hasHigh) text += std::string(hasLow ? " and " : "must be ") + (highIncluded ? "at most " : "less than ") + number(high);
    return text;
}

std::vector<ReferenceField> references(const OpData& operation) {
    OpData copy = operation;
    std::vector<ReferenceField> result;
    std::visit([&](auto& op) {
        describeReferences(op, [&](std::string_view name, auto& slot) {
            result.push_back({ReferenceId{result.size()}, name, Reference(slot)});
        });
    }, copy);
    return result;
}

ReferenceChange setReferenceAt(OpData& operation, std::size_t index, const Reference& value) {
    ReferenceChange outcome = ReferenceChange::NoSuchReference;
    std::size_t position = 0;
    std::visit([&](auto& op) {
        describeReferences(op, [&](std::string_view, auto& slot) {
            if (position++ != index) return;
            if (const auto* typed = std::get_if<std::remove_reference_t<decltype(slot)>>(&value)) {
                slot = *typed;
                outcome = ReferenceChange::Done;
            } else {
                outcome = ReferenceChange::WrongKind;
            }
        });
    }, operation);
    return outcome;
}

std::string edgeName(const EdgeRef& edge, const Document& doc) {
    if (!doc.contains(edge.op)) return {};
    return viaWords(edge.via, doc) + std::string(edgeRoleName(doc.get(edge.op), edge.role));
}

std::string shortName(const CornerRef& corner, const Document& doc) {
    if (!doc.contains(corner.op)) return {};
    std::string_view role = cornerRoleName(doc.get(corner.op), corner.role);
    constexpr std::string_view said = " corner";  // a list of corners need not say that they are
    if (role.ends_with(said)) role.remove_suffix(said.size());
    return viaWords(corner.via, doc) + std::string(role);
}

std::string nameOf(const Reference& reference, const Document& doc) {
    if (const EdgeRef* edge = std::get_if<EdgeRef>(&reference)) {
        if (!doc.contains(edge->op)) return missing(edge->op);
        return edgeName(*edge, doc) + " of " + numbered(edge->op, doc);
    }
    if (const CornerRef* corner = std::get_if<CornerRef>(&reference)) {
        if (!doc.contains(corner->op)) return missing(corner->op);
        return viaWords(corner->via, doc) + std::string(cornerRoleName(doc.get(corner->op), corner->role)) + " of " + numbered(corner->op, doc);
    }
    if (const LineRef* line = std::get_if<LineRef>(&reference)) {
        if (!doc.contains(line->op)) return missing(line->op);
        return "line of " + numbered(line->op, doc);
    }
    const PanelId panel = std::get<PanelId>(reference);
    if (!doc.contains(panel)) return panel == PanelId{} ? "nothing chosen" : "a panel that was removed";
    return doc.get(panel).name;
}

std::string_view label(const OpData& operation) {
    OpData copy = operation;
    return std::visit([](auto& op) { return describe(op, [](std::string_view, Expr&, Limits, Unit) {}); }, copy);
}

std::vector<Field> fields(const OpData& operation) {
    OpData copy = operation;
    std::vector<Field> result;
    std::visit([&](auto& op) {
        describe(op, [&](std::string_view name, Expr& value, Limits limits, Unit unit) {
            result.push_back({FieldId{result.size()}, name, value, limits, unit});
        });
    }, copy);
    return result;
}

std::vector<Dimension> dimensions(const OpData& operation, OpId id) {
    // Field positions follow the order in describe() above.
    if (std::holds_alternative<Rect>(operation)) {
        const OpRef<Rect> rect(id);
        return {{rect.bottom(), FieldId{0}}, {rect.top(), FieldId{0}}, {rect.left(), FieldId{1}}, {rect.right(), FieldId{1}}};
    }
    if (std::holds_alternative<RingSector>(operation)) {
        const OpRef<RingSector> ring(id);
        return {{ring.inner(), FieldId{0}}, {ring.startSide(), FieldId{2}}, {ring.endSide(), FieldId{2}}};
    }
    return {};
}

Expr* fieldAt(OpData& operation, std::size_t index) {
    Expr* found = nullptr;
    std::size_t position = 0;
    std::visit([&](auto& op) {
        describe(op, [&](std::string_view, Expr& value, Limits, Unit) {
            if (position++ == index) found = &value;
        });
    }, operation);
    return found;
}

} // namespace twintailor
