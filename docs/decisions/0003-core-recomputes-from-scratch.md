# 0003. `core` recomputes from scratch; `session` keeps results by version

**Date:** 12 November 2026 (core); version numbers in `session` with the Qt
window, 20-26 November 2026
**Issues/PRs:** #41, #55, PR #103

## Context

Non-destructive editing means an early change must flow through everything
after it. Caches make that easy to get wrong.

## Decision

`evaluate` works everything out from the document every time: no caches,
dirty flags or threads in `core`. `session` keeps results (outlines, mesh,
arrangement) and works them out again only when a version number moves
(`Editor::shapesVersion()`, `Session::sceneVersion()`).

## Why

`core` stays simple and always right; speed is handled in one place, where it
can be measured.

## What we gave up

Finer-grained speed inside `core`, until a measurement asks for it.
