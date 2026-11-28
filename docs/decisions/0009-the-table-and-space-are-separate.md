# 0009. The table and space are separate after a panel's first place

**Date:** 16 November 2026 (0.1.1 "consistency", step 3)
**Issues/PRs:** ROADMAP 0.1.1 step 3

## Decision

A panel gets its place in space once, when it first has an outline
(`session::Editor`, `sim::firstPlace`). After that, moving or turning it on
the table does not move it in space, and the reverse. Placement never changes
a panel's outline.

## Why

Laying out pieces on the table (for cutting) and placing them around the body
(for sewing) are different jobs; tying them together moved cloth that had
been placed with care.

## What we gave up

A table that always shows the panels as they stand in space.
