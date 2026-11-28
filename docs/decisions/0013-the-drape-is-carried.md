# 0013. The drape is carried through edits

**Date:** 26 November 2026
**Issues/PRs:** ROADMAP 0.1.1 step 3, #92, #93, #94, PR #114

## Context

Users rely on drapes that look good even if not perfectly simulated; complex
garments are draped in stages, one layer on another; dropping many flat
panels at once makes them collide for no reason.

## Decision

Nothing starts a panel's cloth again but Reset, and another avatar. A panel
moved in space takes its drape along (`sim::Motion`); a changed outline
carries the drape over by its anchor; undo and redo bring the drape back
(`Session` keeps it per `Editor::step`); an opened file hangs its saved drape;
pulling the cloth changes no document.

## Why

A drape is work the user did; throwing it away on every edit would make
editing a draped garment useless.

## What we gave up

"The cloth always follows from the document alone": the drape depends on
history too. A future Precise mode and freezing are planned on top of this.
