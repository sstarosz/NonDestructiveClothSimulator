# 0005. Our own Vulkan renderer, with Vulkan-Hpp

**Date:** own Vulkan renderer decided 16 November 2026; Vulkan-Hpp 18
November 2026
**Issues/PRs:** ROADMAP 0.1.1 step 5(c)

## Context

0.1 needed a depth-buffered 3D view of the body and the cloth, fast at 4K.

## Decision

`libs/render` draws with Vulkan through Vulkan-Hpp (`vk::raii`), loaded at run
time, without VMA. `Scene` is plain data; `Renderer` draws it into a picture
(for tests) or into a window's swapchain.

## Why

Full control over speed and what is drawn; Vulkan-Hpp gives C++ types and
lifetimes instead of the raw C API. Drawing into a picture makes the renderer
testable without a window. Reason for leaving out VMA: not recorded.

## What we gave up

A ready-made 3D engine. CI has no graphics card, so the drawing tests skip
there.
