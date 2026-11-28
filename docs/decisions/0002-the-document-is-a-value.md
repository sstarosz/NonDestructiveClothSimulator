# 0002. The document is a value; undo is a copy

**Date:** 12 November 2026 (the v2 core)
**Issues/PRs:** see the 0.1.1 "proper undo" step (ROADMAP)

## Context

Everything must stay editable (non-destructive), and undo must be reliable.

## Decision

`Document` holds only what the user authors, nothing computed. It is a value:
copying it gives an independent snapshot, which is what undo keeps. Every
change goes through a verb of `Document`; reading gives `const` access. What is
big and never changed in place (the avatar's mesh) is shared by the copies.

## Why

Undo by copying cannot get out of step with the change it undoes, and there is
no command class per change to write and test.

## What we gave up

Memory for each undo step (small, since big parts are shared).
