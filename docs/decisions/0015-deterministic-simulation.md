# 0015. The simulation is deterministic, in cm, seconds and grams

**Date:** 15 November 2026 (the first cloth)

## Decision

The same document, body and steps give the same drape on the same build.
Lengths are in cm, time in seconds, mass in grams.

## Why

A drape that can be repeated can be tested and its regressions found.

## What we gave up

Easy parallel solvers whose order of work changes the result.
