#pragma once
// Internal: writable access to an operation's formula fields and references.

#include <cstddef>
#include <utility>
#include <vector>

#include "twintailor/core/Fields.h"
#include "twintailor/core/Operations.h"

namespace twintailor {

// The field at `index` in fields() order, or nullptr if there is none.
Expr* fieldAt(OpData& operation, std::size_t index);

enum class ReferenceChange { Done, NoSuchReference, WrongKind };

// Puts `value` in the reference at `index` in references() order.
ReferenceChange setReferenceAt(OpData& operation, std::size_t index, const Reference& value);

// For a copied panel: replaces every operation id that `operation` refers to
// by its partner in `mapping` (original -> copy). Ids not in the mapping are
// kept.
void remapReferences(OpData& operation, const std::vector<std::pair<OpId, OpId>>& mapping);

} // namespace twintailor
