# 0010. Panels grow away from their seams

**Date:** 16 November 2026 (0.1.1 step 3)
**Issues/PRs:** ROADMAP 0.1.1 step 3

## Decision

An arrangement places a panel by its anchor (`Arrangement::anchor`). When the
outline changes, the editor moves the anchor with the sewn edges that kept
their length, on the table and in space, and the drape is carried by the same
anchor. A shape dragged by hand on the table (`Grows::WhereDragged`) keeps its
own coordinates instead.

## Why

A skirt made longer should grow at the hem, not at the waist sewn to the band.

## What we gave up

The simple rule "a panel grows about its middle".
