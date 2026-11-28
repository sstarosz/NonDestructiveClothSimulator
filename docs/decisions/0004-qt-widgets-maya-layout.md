# 0004. Qt 6 Widgets instead of Dear ImGui, laid out as Maya is

**Date:** decided 16 November 2026; the Qt window landed 20 November 2026
(23b50eb); the ImGui window was removed after a comparison feature by feature
**Issues/PRs:** ROADMAP 0.1.1 step 5

## Context

The Dear ImGui window looked bad on a 4K display (small, hard to read text),
and its panels felt like a spreadsheet, not a tool.

## Decision

The window is Qt 6.12 Widgets, close to Autodesk Maya: dockable, collapsible
panels that stack as tabs, a shelf on top, the 3D view left and the table
right, an Outliner over the parameters, an Attribute Editor where operations
feel like layers, a dark theme, little text, units everywhere.

## Why

Proper scaling on high-DPI screens, docking, native dialogs, and a layout the
owner knows from Maya.

## What we gave up

Dear ImGui's simple immediate-mode code, and GLFW. Qt is a big dependency
that comes from the owner's install, not from vcpkg.
