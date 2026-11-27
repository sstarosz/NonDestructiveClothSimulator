# 0008. Catch2, one test file per source file, tests as sentences

**Date:** 16 November 2026
**Issues/PRs:** ROADMAP 0.1.1 step 1, PR #64

## Decision

Tests use Catch2 and read as sentences:
`EXPECT(r.ok(), "cutting is not an error");` (`tests/support/Expect.h`). Unit
tests sit beside each library, one file per source file; tests across
libraries are in `tests/integration/`; benchmarks in `tests/benchmarks/`.
CTest labels: `unit`, `integration`, `benchmark`.

## Why

A test is easy to find from its source file, and a failing test says in
words what broke.

## What we gave up

The project's own tiny check header (`Check.h`), which needed no dependency.
