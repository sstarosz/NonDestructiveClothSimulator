# 0012. Files that were saved keep opening

**Date:** 26 November 2026 (PR #120); confirmed by the owner 27 November 2026,
when breaking changes after 0.1 were allowed
**Issues/PRs:** PR #120, PR #170

## Decision

The texts kept in `core/tests/FileTests.cpp` never change; a change of the
format must still read them, and a new kind of line gets a new case. Operation
names in `File.cpp`'s `typeNames` are never renamed once released. Files of
the earlier name (`.clothcad`, `clothcad 1`) read as any other.

## Why

The owner's own garments must keep opening, even while the C++ API changes
freely.

## What we gave up

A format that is free to change: an old line may have to be read forever.
