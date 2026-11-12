#pragma once
// The document: everything the user authors, and nothing computed.
// Geometry is produced from it by evaluate() (see Evaluate.h).
//
//   Document doc;
//   Param       waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
//   PanelId     band  = doc.add(Panel{.name = "waistband"});
//   OpRef<Rect> rect  = doc.add(band, Rect{.width = waist, .height = 4_cm});
//   doc.set(waist, 80_cm);
//
// You describe a thing with a plain struct (Parameter, Panel, Rect) and get
// back a handle (Param, PanelId, OpRef<Rect>). Every change goes through one
// of the verbs below; reading gives const access only. A Document is a
// value: copying it gives an independent snapshot (this is how undo works).
//
// Verbs throw std::out_of_range when given a handle the document doesn't
// hold. They never compute geometry.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/Expr.h"
#include "core/Ids.h"
#include "core/Operations.h"
#include "core/Units.h"

namespace clothcad {

// A named dimension. New properties (unit, description, limits, presets ...)
// are added here.
struct Parameter {
    std::string name;  // display label
    Expr value;
};

// A pattern piece. Its operations are added with Document::add(panel, op).
struct Panel {
    std::string name;  // display label
};

// What Document::copy returns: the new panel, plus where each original
// operation went. skirt[bandRect] is bandRect's copy inside skirt.
class PanelCopy {
public:
    PanelCopy(PanelId panel, std::vector<std::pair<OpId, OpId>> operations);

    PanelId panel() const { return panel_; }
    operator PanelId() const { return panel_; }

    OpId operator[](OpId original) const;  // throws std::out_of_range if not part of the copy
    template <OperationType T>
    OpRef<T> operator[](OpRef<T> original) const { return OpRef<T>((*this)[OpId(original)]); }

private:
    PanelId panel_;
    std::vector<std::pair<OpId, OpId>> operations_;  // original -> copy
};

class Document {
public:
    // ---- Parameters --------------------------------------------------------
    Param add(Parameter parameter);
    void set(Param parameter, Expr value);
    void rename(Param parameter, std::string name);
    void remove(Param parameter);

    // ---- Panels ------------------------------------------------------------
    PanelId add(Panel panel);
    // Duplicates a panel's operations into a new panel. Expressions are kept
    // as they are, so a field that refers to waist stays linked to waist.
    PanelCopy copy(PanelId source, Panel description);
    void rename(PanelId panel, std::string name);
    void remove(PanelId panel);

    // ---- Operations --------------------------------------------------------
    // Appends an operation to a panel.
    template <OperationType T>
    OpRef<T> add(PanelId panel, T operation) {
        return OpRef<T>(append(panel, OpData(std::in_place_type<T>, std::move(operation))));
    }
    // Changes one field: doc.set(rect, &Rect::height, 60_cm);
    template <OperationType T>
    void set(OpRef<T> operation, Expr T::* field, Expr value) {
        std::get<T>(data(operation)).*field = std::move(value);
    }
    void remove(OpId operation);
    // Moves an operation to position `index` within its own panel.
    void move(OpId operation, std::size_t index);

    // ---- Reading -----------------------------------------------------------
    std::vector<Param> parameters() const;
    std::vector<PanelId> panels() const;
    std::vector<OpId> operations(PanelId panel) const;  // in order

    bool contains(Param parameter) const;
    bool contains(PanelId panel) const;
    bool contains(OpId operation) const;

    const Parameter& get(Param parameter) const;
    const Panel& get(PanelId panel) const;
    const OpData& get(OpId operation) const;
    template <OperationType T>
    const T& get(OpRef<T> operation) const { return std::get<T>(get(OpId(operation))); }

private:
    struct ParameterEntry {
        Param id;
        Parameter parameter;
    };
    struct OperationEntry {
        OpId id;
        OpData data;
    };
    struct PanelEntry {
        PanelId id;
        Panel panel;
        std::vector<OperationEntry> operations;
    };

    OpId append(PanelId panel, OpData data);
    OpData& data(OpId operation);
    ParameterEntry& entry(Param parameter);
    PanelEntry& entry(PanelId panel);
    const PanelEntry* owner(OpId operation) const;

    std::vector<ParameterEntry> parameters_;
    std::vector<PanelEntry> panels_;
    uint32_t nextId_ = 1;
};

} // namespace clothcad
