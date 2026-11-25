#include "clothcad/session/Offers.h"

#include <array>

#include "clothcad/core/Fields.h"
#include "clothcad/core/Text.h"

namespace clothcad::session {

namespace {

constexpr std::array<Offered, 14> everything{{
    {Offer::Rectangle, "Rectangle", "a width and a height", true},
    {Offer::Circle, "Circle", "a radius; a sweep below 360 makes a slice", true},
    {Offer::RingSector, "Ring sector", "for a circle skirt: inner length, sweep, depth", true},
    {Offer::Instance, "Instance", "the finished outline of another panel", true},
    {Offer::Curve, "Curve", "bend an edge into an arc", false},
    {Offer::FreeCurve, "Free curve", "shape an edge with two handles", false},
    {Offer::Line, "Line", "between two points on the outline (easier with the Line tool)", false},
    {Offer::AngledLine, "Angled line", "an internal line from a corner at an angle", false},
    {Offer::Cut, "Cut", "cut along a line and keep one side", false},
    {Offer::Mirror, "Mirror", "complete a half across one of its edges", false},
    {Offer::AdjustPoint, "Adjust point", "move one corner", false},
    {Offer::AddPoint, "Add point", "a new corner on an edge (easier with the Point tool)", false},
    {Offer::Scale, "Scale", "bigger or smaller along x and y (easier with the scale gizmo, R)", false},
    {Offer::SetLength, "Set length", "give one edge its own length, such as a trousers' waist and ankle", false},
}};

// What each starts as.
OpData startOf(const Document& doc, PanelId panel, Offer offer) {
    switch (offer) {
    case Offer::Rectangle: return Rect{.width = 20_cm, .height = 20_cm};
    case Offer::Circle: return Circle{.radius = 20_cm};
    case Offer::RingSector: return RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm};
    case Offer::Instance: {
        Instance instance{};
        for (PanelId other : doc.panels())
            if (!(other == panel) && instance.source == PanelId{}) instance.source = other;
        return instance;
    }
    case Offer::Curve: return Curve{.edge = {}, .bulge = 2_cm};
    case Offer::FreeCurve: return Bezier{.edge = {}, .startHandle = {0_cm, 0_cm}, .endHandle = {0_cm, 0_cm}};
    case Offer::Line: return LineBetween{.from = {{}, 0.5}, .to = {{}, 0.5}};
    case Offer::AngledLine: return Line{.from = {}, .angle = 45_deg};
    case Offer::Cut: {
        // Along the panel's most recent line, if it has one.
        Cut cut{};
        for (OpId op : doc.operations(panel))
            if (std::holds_alternative<Line>(doc.get(op))) cut.along = LineRef{op};
        return cut;
    }
    case Offer::Mirror: return Mirror{.across = {}};
    case Offer::AdjustPoint: return AdjustPoint{.corner = {}, .dx = 0_cm, .dy = 0_cm};
    case Offer::AddPoint: return AddPoint{.at = {{}, 0.5}};
    case Offer::Scale: return Scale{.x = 1.0, .y = 1.0};
    case Offer::SetLength: return SetLength{.edge = {}, .length = 20_cm};
    }
    return Rect{.width = 20_cm, .height = 20_cm};
}

} // namespace

std::vector<Offered> offersFor(const Document& doc, PanelId panel) {
    const bool empty = doc.operations(panel).empty();
    std::vector<Offered> offers;
    for (const Offered& offer : everything)
        if (offer.starting == empty) offers.push_back(offer);
    return offers;
}

Added addOffered(Document& doc, PanelId panel, Offer offer) {
    const OpId added = std::visit([&](auto operation) { return OpId(doc.add(panel, std::move(operation))); }, startOf(doc, panel, offer));
    // An edge, corner or line it needs and has not got yet is chosen next.
    std::optional<ReferenceId> toChoose;
    const std::vector<ReferenceField> slots = references(doc.get(added));
    if (!slots.empty() && !std::holds_alternative<PanelId>(slots[0].value) && nameOf(slots[0].value, doc) == "nothing chosen") toChoose = slots[0].id;
    return {added, toChoose};
}

} // namespace clothcad::session
