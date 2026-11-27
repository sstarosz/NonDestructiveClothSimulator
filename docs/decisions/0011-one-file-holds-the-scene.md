# 0011. One file holds the whole scene

**Date:** 26 November 2026
**Issues/PRs:** #26, #86, #87, PR #120

## Decision

A file holds everything a pattern needs, never a path to another file: the
avatar's mesh is part of the document (`Document::setAvatar`), and putting on
another is an undo step. How the scene was left (`SceneState`: hidden and
deactivated panels, hidden seams, the body hidden or see-through, the drape)
is saved beside the document. It is no undo step, but a change of it counts
as unsaved.

## Why

A file sent to someone else, or opened a month later, must look as it did,
even when the avatar's OBJ has moved.

## What we gave up

Small files: an avatar of 1.5 to 2 million triangles makes a file of 80 to
155 MB (README, "Known limits").
