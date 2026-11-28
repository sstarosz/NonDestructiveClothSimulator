# 0019. A release is made from a tag, as a draft

**Date:** 26 November 2026
**Issues/PRs:** #81, #82, #83, PR #131

## Decision

A release is made only by pushing a tag `v<version>` or `v<version>-rcN`. The
release workflow checks the tag against `project()` in `CMakeLists.txt`, takes
the notes from `CHANGELOG.md`, builds and tests as CI does, checks and zips the
installed folder, and makes a **draft** GitHub Release, which the owner
publishes. Merges to `main` release nothing. The owner pushes tags, or says so
each time before an agent does. The release gets its own pull request, last,
on `feature/release-0.1`.

## Why

A release is a deliberate act of the owner, and what ships is exactly what was
tested.
