#include "internal/References.h"

namespace clothcad {

namespace {

using Mapping = std::vector<std::pair<OpId, OpId>>;

void remap(OpId& id, const Mapping& mapping) {
    for (const auto& [from, to] : mapping)
        if (from == id) {
            id = to;
            return;
        }
}

void remap(EdgeRef& ref, const Mapping& mapping) {
    remap(ref.op, mapping);
    for (OpId& id : ref.via) remap(id, mapping);
}

void remap(CornerRef& ref, const Mapping& mapping) {
    remap(ref.op, mapping);
    for (OpId& id : ref.via) remap(id, mapping);
}

void remap(LineRef& ref, const Mapping& mapping) { remap(ref.op, mapping); }

// One overload per operation type. A type with no references does nothing;
// leaving a type out is a compile error, so none can be forgotten.
void remapIn(Rect&, const Mapping&) {}
void remapIn(Circle&, const Mapping&) {}
void remapIn(RingSector&, const Mapping&) {}
void remapIn(Instance&, const Mapping&) {}
void remapIn(Curve& op, const Mapping& m) { remap(op.edge, m); }
void remapIn(Bezier& op, const Mapping& m) { remap(op.edge, m); }
void remapIn(Line& op, const Mapping& m) { remap(op.from, m); }
void remapIn(LineBetween& op, const Mapping& m) {
    remap(op.from.edge, m);
    remap(op.to.edge, m);
}
void remapIn(Offcut& op, const Mapping& m) { remap(op.of.id, m); }
void remapIn(Cut& op, const Mapping& m) { remap(op.along, m); }
void remapIn(Mirror& op, const Mapping& m) { remap(op.across, m); }
void remapIn(AdjustPoint& op, const Mapping& m) { remap(op.corner, m); }
void remapIn(AddPoint& op, const Mapping& m) { remap(op.at.edge, m); }
void remapIn(Scale&, const Mapping&) {}

} // namespace

void remapReferences(OpData& operation, const Mapping& mapping) {
    std::visit([&](auto& op) { remapIn(op, mapping); }, operation);
}

} // namespace clothcad
