# 0023. Two kinds of internal line stay two operations

**Date:** 27 November 2026

## Context

`Line` and `LineBetween` both make an internal line that a `Cut` can follow.

## Decision

Keep both. `Line` ("Angled line" in the window) is a corner and an angle; its
end is where it meets the outline. It is what an angled hem needs: the angle is
what is edited. `LineBetween` ("Line" in the window, the Line tool) is two
points on edges. Possible later: let `Line` start
from any point on an edge, and name the structs as the window does
(`scratch/line-names-issues.md`).

## Why

They differ in what the user controls; one struct with two kinds of ends would
only move the choice inside it.

## What we gave up

One operation fewer. Look again when internal lines get free points, several
segments and curves.
