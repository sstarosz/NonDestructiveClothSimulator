# Adding an operation: every place it must appear

An operation is one step in a panel's chain (a `Rect`, a `Cut`, a `Mirror`).
Before adding one, say which existing operation cannot carry it: a new field
on an existing struct is usually better than a new struct.

1. `libs/core/include/twintailor/core/Operations.h`: the struct, its `Names`
   (roles as enums), and the `OpData` variant.
2. `libs/core/src/Fields.cpp`: `describe` (label, fields, limits, units),
   `describeReferences`, and `dimensions` if a field is an edge's length.
3. `libs/core/src/References.cpp`: remapping when a panel is copied.
4. `libs/core/src/Evaluate.cpp`: an `apply` overload. It never throws: a
   problem is an error of the result, in a sentence that says what to do.
5. `libs/core/src/File.cpp`: its name in `typeNames` (same order as `OpData`,
   never renamed once released, see
   [decision 0012](decisions/0012-saved-files-keep-opening.md)) and `extras`
   if it holds more than formulas and references.
6. Tests: `libs/core/tests/EvaluateTests.cpp` (what it does, how it fails),
   `ReferencesTests.cpp` (it survives a copy) and `FileTests.cpp` (it
   survives save and open, with a text of its own).
7. If it has a tool: a gesture struct in `libs/session/Drafting.h`, handled
   in `Drafting::on(...)` (with a test in `DraftingTests.cpp`), reported by
   `TableView`, with an icon, a hint and a shelf entry (`Shelf.cpp`). If it
   can be added from a list, an entry in `session/Offers`.
