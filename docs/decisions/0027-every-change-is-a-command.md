# 0027. Every change is a command; undo is a copy

**Date:** 27 November 2026 (five trial commands built on 28 November; the rest to come)
**Issues/PRs:** none

## Context

Document edits are put together in the window and in `session::Drafting`, out
of reach of scripts. A pull and Reset cannot be undone, and after a crash only
a recovery copy up to 30 seconds old is left.

## Decision

- Every action that changes the work is a command: a plain struct in
  `session` that holds all it needs. `session` runs it, and that is the only
  way to change anything; the window cannot edit the document itself.
- A command runs on a copy. If it fails, nothing changes.
- Undo keeps copies, not hand-written reversals. The history holds the
  document and the drape, so that a pull and Reset can be undone.
- A drag is begun, updated and then committed or cancelled: one undo step.
- Selection, tool and camera are not commands. `Drafting` turns a gesture and
  the selection into a command.
- Commands can be stored: after a crash they are run again on the last copy of
  the work. The drape is stored as data, not simulated again.

It is built before 0.1, in this order: five commands that test the design (set
a field, drag a corner, a mirrored instance, delete, pull), the other edits one
area at a time, the drape in the history, then the stored commands. Until a
rule is built, [`architecture.md`](../architecture.md) says what holds today.

## Why

The window only builds a command; scripts and tests do what the window does.
A stored command gives recovery up to the last action. A copy of the document
is a correct undo for every command and costs little: what is big and does not
change is shared by the copies ([0002](0002-the-document-is-a-value.md)).

Not chosen: better recovery copies alone (nothing for scripts); plain
functions in `session` (nothing to store); commands that each undo themselves
(a reversal to write and to get wrong for every command).

## What we gave up

Every command has to be stored, read back and tested. A command must name
fields without text, which panel placements cannot yet. A command that crashes
the program has to be left out when the commands are run again.
