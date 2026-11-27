# 0014. The cloth runs on its own thread, at most 60 steps a second

**Date:** 16 November 2026 (the thread, 0.1.1); 26 November 2026 (the cap and waking)
**Issues/PRs:** #106, #107, PR #108

## Decision

The cloth runs in `session::Simulation` on its own thread; nothing else
touches it, and the interface never waits for it. It steps at most 60 times a
second; a held cloth runs even when paused; changes between steps do not make
it step early.

## Why

The window must stay at the screen's pace (240 Hz on the owner's display)
whatever the cloth costs, and stepping faster than the clock made the cloth
run ahead.

## What we gave up

Stepping as fast as the machine allows when nothing else is happening.
