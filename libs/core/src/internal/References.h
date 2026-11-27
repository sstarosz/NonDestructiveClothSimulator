#pragma once
// Internal: rewriting the references an operation holds, used when a panel
// is copied. Every operation type that holds an EdgeRef, CornerRef or
// LineRef must be handled in References.cpp.

#include <utility>
#include <vector>

#include "twintailor/core/Operations.h"

namespace twintailor {

// Replaces every operation id found in `operation`'s references by its
// partner in `mapping` (original -> copy). Ids not in the mapping are kept.
void remapReferences(OpData& operation, const std::vector<std::pair<OpId, OpId>>& mapping);

} // namespace twintailor
