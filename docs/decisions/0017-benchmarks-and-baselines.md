# 0017. Benchmarks against baselines, run on the owner's machine

**Date:** 15 November 2026 (wanted); 26 November 2026 (baselines to trust)
**Issues/PRs:** #57, #68, #88, PR #101, #109, #116

## Decision

Frame and step times are benchmarks with limits and baselines
(`tests/benchmarks/`), measured against a fixed piece of arithmetic. They run
on the owner's idle machine, not on CI. New baselines are written only when
the owner agrees, never to make a failure go away.

## Why

Speed of the interface and the cloth is a feature; shared CI runners are too
noisy for the limits.

## What we gave up

A speed check on every pull request.
