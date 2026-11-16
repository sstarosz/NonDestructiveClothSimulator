#pragma once
// Internal: writable access to an operation's formula fields by position.

#include <cstddef>

#include "clothcad/core/Fields.h"
#include "clothcad/core/Operations.h"

namespace clothcad {

// The field at `index` in fields() order, or nullptr if there is none.
Expr* fieldAt(OpData& operation, std::size_t index);

enum class ReferenceChange { Done, NoSuchReference, WrongKind };

// Puts `value` in the reference at `index` in references() order.
ReferenceChange setReferenceAt(OpData& operation, std::size_t index, const Reference& value);

} // namespace clothcad
