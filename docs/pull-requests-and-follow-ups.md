# Pull requests and follow-ups

## Follow-ups: limits and improvements become issues

GitHub is the list of follow-up tasks
([decision 0020](decisions/0020-github-issues-are-the-follow-ups.md)). A
`scratch/*-issues.md` file is only a draft until its sections are filed
(`scratch/` is not in git).

Do not leave a limit, a doubt or a worthwhile improvement only in the chat or
in a code comment. When a change shows one that is not part of the request (a
cleanup around the code you touched, something you could not check, a limit
of what you built), keep the change small and note it for later:

1. Write it in `scratch/<topic>-issues.md` (for example
   `scratch/cloth-tab-issues.md`). Start the file with a few lines on where
   the issues were found, then one `## Title` section per issue, sections
   separated by `---`, each with a `**Labels:** a, b` line (labels the repo
   has: see `gh label list`; unknown ones are left out), then **What**,
   **Why** or **Idea**, and **Done when** (a test or a check that tells it is
   finished).
2. Say which cleanups you did not do, and why, in your report, and point to
   the file.
3. Check what would be made with
   `python scratch/make_issues.py scratch/<topic>-issues.md`. Only the owner
   decides to create them: add `--create` when asked to. It writes
   `**Issue:** #N` under each title it files, never files a section twice,
   links a title that is already on GitHub instead of making it again, and
   moves a file whose sections are all filed to `scratch/filed/`.
4. What is open and what is done is read from GitHub, not from the files:
   `python scratch/make_issues.py --status`.
5. A pull request that finishes an issue says `Closes #N`; one that only
   moves it forward says `Refs #N` and what is still open on it.
6. A limit users must know is also written in
   [`known-limits.md`](known-limits.md)
   or `ROADMAP.md`; the issue is for the work to remove it.
7. A cleanup that is small, inside the code you already changed and covered
   by tests is done now, not noted.

## Pull requests: labels and a milestone

Every pull request is opened with labels and a milestone, and its description
follows `.github/pull_request_template.md`:

    gh pr create --milestone 0.1 --label Enhancement --label Render --label Qt ...

- **Labels** are the repo's own (`gh label list`); never make a new one. Start
  from the labels of the issues it closes, then make them true of the change:
  its kind (`Bug`, `Enhancement`, `Documentation`), `Performance` or `UI/UX`
  where they apply, and each part it changes (`Core`, `Sim`, `Session`,
  `Render`, `Qt`). `Tests` is for work about the tests themselves, not for the
  tests every change brings.
- **Milestone** is that of the issues it closes or moves forward (`0.1`,
  `0.2`, `0.x`); with no issue, the one `ROADMAP.md` puts the work in. If that
  is not clear, ask.
- Check with `gh pr view <number> --json labels,milestone`, and say in the
  report what was set.
- A pull request that makes a new choice adds its record to
  [`decisions/`](decisions/README.md); one that changes a rule changes
  [`architecture.md`](architecture.md).
