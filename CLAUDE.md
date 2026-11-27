# Instructions for Claude

This is Twintailor: a parametric pattern CAD for clothing in C++23, with a 2D
table, a 3D view and a cloth simulation. `README.md` says why it exists,
[`docs/user-guide.md`](docs/user-guide.md) what it does,
[`docs/known-limits.md`](docs/known-limits.md) what it cannot do yet and
`ROADMAP.md` what is planned.

**Before changing code, read [`docs/architecture.md`](docs/architecture.md)**:
what each library holds and may use, and the rules that must stay true. The
reasons behind them are in [`docs/decisions/`](docs/decisions/README.md).

## Who it is for

First artists who dress 3D characters; also people who sew real clothes. When
their needs clash, the artists win. The program stays with designing clothes.

## What wins when goals clash

The higher one wins
([decision 0026](docs/decisions/0026-what-wins-and-whose-the-drape-is.md)):

1. **Never lose work.** A crash, closing, saving or one wrong click never
   throws away the pattern or the drape.
2. **Non-destructive.** Nothing is baked into the pattern: an early step can be
   changed and everything after it follows (the waist, changed after the
   armholes were cut, leaves the cut in place).
3. **Easy and forgiving to use.**
4. **Fast.** The interface and the cloth as fast as they can be; nothing is
   worked out per frame that has not changed.
5. **More features.**

Two rules about the cloth:

- **The drape is the artist's work**, not only output: it is saved, can be
  undone, and is kept through edits of the pattern as well as it can be.
- **Realistic by default; the artist has the last word.** The simulation aims
  for true folds, and the artist may hold, pull or freeze cloth into a shape
  physics would not give.

These are goals. Where the program falls short of them is in
[`docs/known-limits.md`](docs/known-limits.md).

## How the code should be

1. **Strong types, no string lookups.** Things are named by typed handles,
   never by text or bare indices. Strings are for people to read.
2. **Explicit types and a small, clean API.** Few verbs, each doing one
   thing. Hide implementation in a library's `src/` or in the `.cpp`.
3. **Code that reads like a DSL.** A plain struct describes a thing, a verb
   takes it and returns a handle:
   `OpRef<Rect> rect = doc.add(panel, Rect{.width = waist / 2, .height = 4_cm});`
4. **Easy to test.** Everything that matters works without a window.
5. **Viewport-driven interface.** Work happens on the table and in the 3D
   view with a small shelf; the docks only show and fine-tune. Little text:
   units on every number, hints in the help line.
6. **Small steps.** One contained piece at a time, with its limits written
   down, not a large rewrite.

## Look before you add

- Before writing a helper, search for one that exists.
- Before adding a gesture, an operation, a verb, a setter or a public type,
  say which existing one cannot carry it. A field on an existing struct is
  usually better than a new function.
- Every new public name in a header is named in the pull request with the
  reason it is needed.
- Delete what your change leaves unused, in the same change.

## Style

- Names are plain words for what a thing is (`placedNow`, `carriedFrom_`,
  `nearness_`), not abbreviations or pattern names. Members end in `_`.
- Each header opens with a few lines on what it is for and a short usage
  example.
- Comments say why, or what a reader cannot see from the code. They say what
  is, never what changed ("now", "no longer", "instead of before"): the
  history is in git. No issue or PR numbers in source comments; a test may
  name the issue it guards.
- A comment that states behaviour uses the same words as the test that checks
  it, so that searching one finds the other.
- Messages to the user are whole sentences that say what to do next.
- No macros (the tests' `EXPECT`, `EXPECT_NEAR`, `EXPECT_THROWS` are the only
  ones), no global state, no inheritance where a `std::variant` or a table
  will do. Errors a caller must handle are `std::expected<T, std::string>`;
  prefer `std::optional` and small structs to out-parameters.
- Match the code around you before inventing a new way.

## Testing and checking

- Every feature and every fix arrives with a test that fails without it.
  Tests use Catch2 and read as sentences:
  `EXPECT(r.ok(), "cutting is not an error");` (`tests/support/Expect.h`).
  Unit tests sit beside each library, one file per source file; tests across
  libraries in `tests/integration/`; benchmarks in `tests/benchmarks/`. CTest
  labels: `unit`, `integration`, `benchmark`.
- Build and run:

      cmake --preset x64-win-msvc2026
      cmake --build --preset x64-win-msvc2026-release
      ctest --preset x64-win-msvc2026-release

- After a change to `sim`, `session` or the drawing, run the benchmarks; see
  [`docs/benchmarks.md`](docs/benchmarks.md). New baselines only when the
  owner agrees.
- The owner builds with MSVC on Windows. If you verify with another
  compiler, or cannot run the window, say exactly that. Never report
  something as working that you have not run or seen; name the part you
  could not check.
- Warnings are defects and stop the build (`TWINTAILOR_WARNINGS_AS_ERRORS`);
  fix the warning, never turn the option off to get past it.

## How to work with the owner

- Start on a clear request without asking for permission; ask only when a
  wrong guess would be expensive. When a requirement is unclear and you had
  to choose, say what you chose and why.
- Report briefly: what changed, how to use it, what is limited, what was and
  was not verified. Plain words; English is not the owner's first language.
- When something is a real limitation, write it down
  (`docs/known-limits.md`, `ROADMAP.md`) instead of hiding it; the work to
  remove it becomes an issue
  draft ([`docs/pull-requests-and-follow-ups.md`](docs/pull-requests-and-follow-ups.md)).
- A choice made with the owner gets a record in `docs/decisions/`; a rule
  that changes is changed in `docs/architecture.md`, in the same pull request.
- Keep `docs/user-guide.md`, `docs/known-limits.md` and `ROADMAP.md` current
  when a roadmap step lands or a limit goes away.
- `README.md` stays short, for someone new: what a feature does goes to the
  user guide, what it cannot do to the known limits. One fact has one place;
  link to it instead of repeating it.
- Do not leave stray files in the project, do not delete the owner's files,
  and do not add dependencies without saying so first. They go in
  `vcpkg.json` and are found with `find_package`.
- If a request conflicts with the rules (a string lookup, a cache in `core`,
  computing in a window), say so and offer the way that keeps them.

## How-tos

- [Adding an operation](docs/adding-an-operation.md): every place it must
  appear.
- [Pull requests and follow-ups](docs/pull-requests-and-follow-ups.md):
  labels, milestones, and limits drafted as issues.
- [Benchmarks](docs/benchmarks.md): when to run them, and baselines.
