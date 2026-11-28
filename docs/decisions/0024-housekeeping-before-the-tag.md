# 0024. Housekeeping before the 0.1 tag; breaking changes after it

**Date:** 27 November 2026
**Issues/PRs:** `scratch/plans/housekeeping-review.md`,
`scratch/plans/housekeeping-prompts.md`

## Decision

Before the tag: decisions and architecture written down, rules turned into
checks, a second CI job, and the compressions that change no behaviour
(geometry, `Document` setters, `Session` view state, `Drafting` split,
`render` layers, compile time). Then `v0.1.0-rc2` and the hand checks again.
After 0.1, breaking the C++ API is fine (nobody else uses it yet); old files
keep opening ([0012](0012-saved-files-keep-opening.md)).

## Why

The code is mostly AI-written; 0.2 should start from a smaller API, recorded
choices and checks that catch slop.

## What we gave up

An earlier tag.
