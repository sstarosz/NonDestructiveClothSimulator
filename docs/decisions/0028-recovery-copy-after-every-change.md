# 0028. After a crash the work comes back from a copy written after every change

**Date:** 28 November 2026
**Issues/PRs:** none

## Context

[0027](0027-every-change-is-a-command.md) planned to store commands and run
them again after a crash, to get the work back up to the last action. A pattern
is turned into text in well under a millisecond. Stored commands need a saved
form for each command, a replay that is only sure to give the same result in
the same version of the program, and a way past a command that crashes it.

## Decision

- A recovery copy is written after every change: a command, a finished drag or
  typing, undo and redo, what is hidden or frozen, and the cloth as it comes to
  rest. While the cloth runs or is pulled, only a change of the document writes
  a copy, so its motion writes nothing: the copy from before it stays.
- In the recovery folder the avatar is in a file of its own, written only when
  it is another one. A saved file still holds the whole scene
  ([0011](0011-one-file-holds-the-scene.md)).
- The undo history does not come back after a crash: files do not keep it
  either.
- To find the cause of a crash, the program records its last actions and the
  error and writes a crash dump, on the user's computer only; nothing is sent.
  Not built yet.
- Commands are not stored. This replaces that part of 0027.

## Why

Never losing work comes first
([0026](0026-what-wins-and-whose-the-drape-is.md)), and a copy has far fewer
ways to go wrong than a replay. An undo history that is not kept in a saved
file is of little use after a crash.

## What we gave up

No undo history after a crash. A simulation that was running goes back to its
start, or to the last change. A very detailed avatar is written again after
each save. Stored commands can be taken up again if scripts need them: the
commands stay as data.
