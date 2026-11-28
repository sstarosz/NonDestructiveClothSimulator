# 0001. Start again, with a small typed API that reads like a DSL

**Date:** about 10 November 2026 (the v2 core first compiled on 12 November)
**Issues/PRs:** none (before GitHub)

## Context

The first prototype was written mostly by AI and had become hard to
understand. Its first API sketch named things by strings.

## Decision

Start again, from example garments written as code first. A plain struct
describes a thing, a verb takes it and returns a typed handle:

    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist / 2, .height = 4_cm});

Things are named by handles (`Param`, `PanelId`, `OpId`, `SeamId`,
`EdgeRef`, `CornerRef`, `LineRef`, `OpRef<T>`), never by text. A parameter is
a struct from the start, so that it can grow.

## Why

Strong types catch mistakes at compile time; a small API is easy to test,
can carry commands and higher-level tests later, and could be wrapped for
Python one day.

## What we gave up

The old prototype's code; string lookups that are quicker to write.
