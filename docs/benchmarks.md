# Benchmarks

What a frame and a cloth step may cost, and how it is measured, is in
`README.md` ("Performance"); why they run on the owner's machine is
[decision 0017](decisions/0017-benchmarks-and-baselines.md). The rules for
changing code:

- **Run the benchmarks** (CTest label `benchmark`, optimised build,
  `ctest --preset x64-win-msvc2026-benchmarks`) after any change to `sim`,
  `session` or the drawing, with Twintailor closed and the machine idle.
- **Write new baselines** (`--update`) only when the owner agrees, and say
  which numbers moved and why; never to make a failure go away.
- Before blaming a change for a failure, run the same benchmark on `main` on
  the same machine: the yardstick shows when the machine was busy.
