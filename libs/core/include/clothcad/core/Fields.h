#pragma once
// An operation seen without knowing its type: its label and its formula
// fields in order. This is what the window's operation list is built from.
//
//   for (const Field& field : fields(doc.get(op)))
//       show(field.label, field.value);
//   doc.set(op, field.id, newValue);

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "clothcad/core/Operations.h"
#include "clothcad/core/Units.h"

namespace clothcad {

// Which formula field of an operation: its position in fields().
struct FieldId {
    std::size_t index = 0;
    friend bool operator==(FieldId, FieldId) = default;
};

// The values a formula field accepts. This is the contract for the field:
// evaluate() refuses an operation whose field breaks it, and a view can show
// the rule and stop the user before that happens.
struct Limits {
    double low;         // -infinity when there is no lower limit
    bool lowIncluded;   // whether `low` itself is allowed
    double high;        // +infinity when there is no upper limit
    bool highIncluded;

    static Limits any();                                 // every number
    static Limits moreThan(double low);                  // low < value
    static Limits moreThanUpTo(double low, double high); // low < value <= high
    static Limits between(double low, double high);      // low <= value <= high
    static Limits strictlyBetween(double low, double high);  // low < value < high

    bool allows(double value) const;
    std::string rule() const;  // "must be greater than 0"; empty when every number is allowed
};

struct Field {
    FieldId id;
    std::string_view label;  // "Width"
    Expr value;
    Limits limits;
    Unit unit;  // what it measures: shown and typed as 26 cm, 25°, 66.7 %
};

std::string_view label(const OpData& operation);  // "Rectangle"
std::vector<Field> fields(const OpData& operation);

// ---- References: the edge, corner, line or panel an operation works on ----
//
//   for (const ReferenceField& slot : references(doc.get(op)))
//       show(slot.label, nameOf(slot.value, doc));
//   doc.set(op, slot.id, pickedEdge);

using Reference = std::variant<EdgeRef, CornerRef, LineRef, PanelId>;

// Which reference of an operation: its position in references().
struct ReferenceId {
    std::size_t index = 0;
    friend bool operator==(ReferenceId, ReferenceId) = default;
};

struct ReferenceField {
    ReferenceId id;
    std::string_view label;  // "Edge", "From corner"
    Reference value;         // its type says what kind of thing may be put here
};

std::vector<ReferenceField> references(const OpData& operation);

class Document;

// What a reference points at, in words: "top edge of 1. Rectangle",
// "mirrored bottom edge of 1. Rectangle", "line of 3. Line", a panel's name.
// Says so when nothing is chosen yet or the target has been removed.
std::string nameOf(const Reference& reference, const Document& doc);

// What an edge is called without saying which operation made it: "top edge", "mirrored top edge".
// Empty when the operation that made it was removed.
std::string edgeName(const EdgeRef& edge, const Document& doc);

// An edge whose length is one of the operation's fields: a rectangle's top
// edge and its Width. A view uses this to show what drives an edge.
struct Dimension {
    EdgeRef edge;
    FieldId field;
};

// `id` is the operation's own id, needed to name its edges.
std::vector<Dimension> dimensions(const OpData& operation, OpId id);

} // namespace clothcad
