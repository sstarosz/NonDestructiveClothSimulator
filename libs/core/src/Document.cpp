#include "clothcad/core/Document.h"

#include <algorithm>
#include <stdexcept>

#include "internal/FieldAccess.h"
#include "internal/References.h"

namespace clothcad {

namespace {

[[noreturn]] void unknown(const char* what) {
    throw std::out_of_range(std::string("clothcad::Document: unknown ") + what);
}

} // namespace

// ---- PanelCopy --------------------------------------------------------------

PanelCopy::PanelCopy(PanelId panel, std::vector<std::pair<OpId, OpId>> operations)
    : panel_(panel), operations_(std::move(operations)) {}

OpId PanelCopy::operator[](OpId original) const {
    for (const auto& [from, to] : operations_)
        if (from == original) return to;
    throw std::out_of_range("clothcad::PanelCopy: operation is not part of the copied panel");
}

// ---- Private lookups --------------------------------------------------------

Document::ParameterEntry& Document::entry(Param parameter) {
    for (ParameterEntry& e : parameters_)
        if (e.id == parameter) return e;
    unknown("parameter");
}

Document::PanelEntry& Document::entry(PanelId panel) {
    for (PanelEntry& e : panels_)
        if (e.id == panel) return e;
    unknown("panel");
}

const Document::PanelEntry* Document::owner(OpId operation) const {
    for (const PanelEntry& p : panels_)
        for (const OperationEntry& o : p.operations)
            if (o.id == operation) return &p;
    return nullptr;
}

OpData& Document::data(OpId operation) {
    for (PanelEntry& p : panels_)
        for (OperationEntry& o : p.operations)
            if (o.id == operation) return o.data;
    unknown("operation");
}

OpId Document::append(PanelId panel, OpData data) {
    PanelEntry& p = entry(panel);
    const OpId id{nextId_++};
    p.operations.push_back({id, std::move(data), true});
    return id;
}

// ---- Parameters -------------------------------------------------------------

Param Document::add(Parameter parameter) {
    const Param id{nextId_++};
    parameters_.push_back({id, std::move(parameter)});
    return id;
}

void Document::set(Param parameter, Expr value) { entry(parameter).parameter.value = std::move(value); }

void Document::rename(Param parameter, std::string name) { entry(parameter).parameter.name = std::move(name); }

void Document::remove(Param parameter) {
    entry(parameter);  // throws if unknown
    std::erase_if(parameters_, [&](const ParameterEntry& e) { return e.id == parameter; });
}

// ---- Panels -----------------------------------------------------------------

PanelId Document::add(Panel panel) {
    const PanelId id{nextId_++};
    panels_.push_back({id, std::move(panel), {}});
    return id;
}

PanelCopy Document::copy(PanelId source, Panel description) {
    // Copy the operations first: adding a panel may move the source in memory.
    std::vector<OperationEntry> operations = entry(source).operations;

    std::vector<std::pair<OpId, OpId>> mapping;
    mapping.reserve(operations.size());
    for (OperationEntry& o : operations) {
        const OpId fresh{nextId_++};
        mapping.emplace_back(o.id, fresh);
        o.id = fresh;
    }
    // A curve in the copy must bend the copy's rectangle, not the original's.
    for (OperationEntry& o : operations) remapReferences(o.data, mapping);

    const PanelId id{nextId_++};
    panels_.push_back({id, std::move(description), std::move(operations)});
    return PanelCopy(id, std::move(mapping));
}

void Document::rename(PanelId panel, std::string name) { entry(panel).panel.name = std::move(name); }

void Document::place(PanelId panel, double x, double y) {
    Panel& p = entry(panel).panel;
    p.x = x;
    p.y = y;
}

void Document::turn(PanelId panel, double degrees) { entry(panel).panel.angle = degrees; }

void Document::arrange(PanelId panel, Arrangement arrangement) { entry(panel).panel.arrangement = arrangement; }

void Document::setConstruction(PanelId panel, bool construction) { entry(panel).panel.construction = construction; }

void Document::remove(PanelId panel) {
    entry(panel);  // throws if unknown
    std::erase_if(panels_, [&](const PanelEntry& e) { return e.id == panel; });
    std::erase_if(seams_, [&](const SeamEntry& e) { return e.seam.a.panel == panel || e.seam.b.panel == panel; });
}

// ---- Seams ------------------------------------------------------------------

SeamId Document::add(Seam seam) {
    entry(seam.a.panel);  // throws if unknown
    entry(seam.b.panel);
    const SeamId id{nextId_++};
    seams_.push_back({id, std::move(seam)});
    return id;
}

void Document::set(SeamId seam, Seam value) {
    entry(value.a.panel);
    entry(value.b.panel);
    for (SeamEntry& e : seams_)
        if (e.id == seam) {
            e.seam = std::move(value);
            return;
        }
    unknown("seam");
}

void Document::remove(SeamId seam) {
    if (std::erase_if(seams_, [&](const SeamEntry& e) { return e.id == seam; }) == 0) unknown("seam");
}

std::vector<SeamId> Document::seams() const {
    std::vector<SeamId> ids;
    for (const SeamEntry& e : seams_) ids.push_back(e.id);
    return ids;
}

bool Document::contains(SeamId seam) const {
    return std::any_of(seams_.begin(), seams_.end(), [&](const SeamEntry& e) { return e.id == seam; });
}

const Seam& Document::get(SeamId seam) const {
    for (const SeamEntry& e : seams_)
        if (e.id == seam) return e.seam;
    unknown("seam");
}

// ---- Operations -------------------------------------------------------------

void Document::set(OpId operation, FieldId field, Expr value) {
    Expr* target = fieldAt(data(operation), field.index);
    if (!target) throw std::out_of_range("clothcad::Document: the operation has no such field");
    *target = std::move(value);
}

void Document::set(OpId operation, ReferenceId reference, Reference value) {
    switch (setReferenceAt(data(operation), reference.index, value)) {
    case ReferenceChange::Done: return;
    case ReferenceChange::NoSuchReference: throw std::out_of_range("clothcad::Document: the operation has no such reference");
    case ReferenceChange::WrongKind: throw std::invalid_argument("clothcad::Document: that kind of thing cannot be used for this reference");
    }
}

void Document::remove(OpId operation) {
    for (PanelEntry& p : panels_) {
        const auto erased = std::erase_if(p.operations, [&](const OperationEntry& o) { return o.id == operation; });
        if (erased > 0) return;
    }
    unknown("operation");
}

void Document::setEnabled(OpId operation, bool enabled) {
    for (PanelEntry& p : panels_)
        for (OperationEntry& o : p.operations)
            if (o.id == operation) {
                o.enabled = enabled;
                return;
            }
    unknown("operation");
}

bool Document::enabled(OpId operation) const {
    if (const PanelEntry* p = owner(operation))
        for (const OperationEntry& o : p->operations)
            if (o.id == operation) return o.enabled;
    unknown("operation");
}

void Document::move(OpId operation, std::size_t index) {
    for (PanelEntry& p : panels_) {
        auto& ops = p.operations;
        const auto it = std::find_if(ops.begin(), ops.end(), [&](const OperationEntry& o) { return o.id == operation; });
        if (it == ops.end()) continue;
        if (index >= ops.size()) throw std::out_of_range("clothcad::Document: move index is past the end of the panel");
        const auto target = ops.begin() + static_cast<std::ptrdiff_t>(index);
        if (it < target) std::rotate(it, it + 1, target + 1);
        else std::rotate(target, it, it + 1);
        return;
    }
    unknown("operation");
}

// ---- Reading ----------------------------------------------------------------

std::vector<Param> Document::parameters() const {
    std::vector<Param> ids;
    ids.reserve(parameters_.size());
    for (const ParameterEntry& e : parameters_) ids.push_back(e.id);
    return ids;
}

std::vector<PanelId> Document::panels() const {
    std::vector<PanelId> ids;
    ids.reserve(panels_.size());
    for (const PanelEntry& e : panels_) ids.push_back(e.id);
    return ids;
}

std::vector<OpId> Document::operations(PanelId panel) const {
    for (const PanelEntry& p : panels_) {
        if (!(p.id == panel)) continue;
        std::vector<OpId> ids;
        ids.reserve(p.operations.size());
        for (const OperationEntry& o : p.operations) ids.push_back(o.id);
        return ids;
    }
    unknown("panel");
}

PanelId Document::panelOf(OpId operation) const {
    if (const PanelEntry* p = owner(operation)) return p->id;
    unknown("operation");
}

bool Document::contains(Param parameter) const {
    return std::any_of(parameters_.begin(), parameters_.end(), [&](const ParameterEntry& e) { return e.id == parameter; });
}

bool Document::contains(PanelId panel) const {
    return std::any_of(panels_.begin(), panels_.end(), [&](const PanelEntry& e) { return e.id == panel; });
}

bool Document::contains(OpId operation) const { return owner(operation) != nullptr; }

const Parameter& Document::get(Param parameter) const {
    for (const ParameterEntry& e : parameters_)
        if (e.id == parameter) return e.parameter;
    unknown("parameter");
}

const Panel& Document::get(PanelId panel) const {
    for (const PanelEntry& e : panels_)
        if (e.id == panel) return e.panel;
    unknown("panel");
}

const OpData& Document::get(OpId operation) const {
    if (const PanelEntry* p = owner(operation))
        for (const OperationEntry& o : p->operations)
            if (o.id == operation) return o.data;
    unknown("operation");
}

} // namespace clothcad
