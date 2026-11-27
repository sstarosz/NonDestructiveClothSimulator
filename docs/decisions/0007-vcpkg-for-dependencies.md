# 0007. Dependencies from vcpkg; Qt from the owner's install

**Date:** 16 November 2026 (replacing CMake `FetchContent`)
**Issues/PRs:** ROADMAP 0.1.1 step 1

## Decision

Libraries come from vcpkg (`vcpkg.json`, with a `builtin-baseline`) and are
found with `find_package`. Qt comes from the owner's install
(`C:/Qt/6.12.0/msvc2022_64`), and on CI from `install-qt-action`. A new
dependency is added only after asking the owner.

## Why

vcpkg was already on the owner's machine, caches what it built, and pins
versions; building Qt through vcpkg is very slow.

## What we gave up

`FetchContent`'s "nothing to install first".
