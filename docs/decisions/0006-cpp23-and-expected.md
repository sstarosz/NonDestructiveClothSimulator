# 0006. C++23; errors a caller must handle are `std::expected`

**Date:** 16 November 2026 (with the move to Qt)
**Issues/PRs:** ROADMAP 0.1.1 step 1

## Decision

The code is C++23 (`CMAKE_CXX_STANDARD 23`). Errors a caller must handle come
back as `std::expected<T, std::string>`; `std::optional` and small structs are
preferred to out-parameters. Evaluation, meshing and the cloth never throw;
document verbs throw only for an unknown handle.

## Why

`std::expected` makes a refusal part of the type, so it cannot be ignored by
accident, and the message is ready to show to the user.

## What we gave up

Older compilers (GCC 11 has no `<expected>`).
