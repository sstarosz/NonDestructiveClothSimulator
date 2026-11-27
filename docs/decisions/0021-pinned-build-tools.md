# 0021. CI and releases use pinned tools

**Date:** 27 November 2026
**Issues/PRs:** #162, PR #163

## Decision

The workflows name exact tools: the Vulkan SDK version and each action at a
commit, with its version in a comment. `cmake/CheckWorkflows.cmake`, run as a
test, refuses a floating name.

## Why

A release must be built with the same tools as the run that tested it.

## What we gave up

Updates that come by themselves.
