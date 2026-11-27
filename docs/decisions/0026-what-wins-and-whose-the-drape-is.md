# 0026. What wins when goals clash; the drape is the artist's work

**Date:** 27 November 2026
**Issues/PRs:** none

## Context

Before the review for 0.1 the goals were written down. Two of them pull
against each other: nothing is baked in, yet a drape holds work done by hand
that a new simulation cannot bring back.

## Decision

- Artists who dress 3D characters come first, people who sew real clothes
  second.
- When goals clash, the higher one wins: never lose work, non-destructive,
  easy and forgiving to use, fast, more features.
- The pattern is exact and non-destructive. The drape is the artist's work: it
  is saved, can be undone, and is kept through edits of the pattern as well as
  it can be.
- A pull is undone by keeping the drape as it was before the pull and as it is
  after it; the movement of the pull is not stored.
- The simulation is realistic by default; the artist may hold, pull or freeze
  cloth into a shape physics would not give.

## Why

A complex or deliberately unrealistic garment cannot be simulated again from
the flat panels: it is arranged, simulated and corrected in stages.

## What we gave up

"The cloth always follows from the document alone" (see
[0013](0013-the-drape-is-carried.md)). Keeping drapes for undo costs memory.
Pulls and Reset cannot be undone yet, and there is no freeze
([known limits](../known-limits.md)).
