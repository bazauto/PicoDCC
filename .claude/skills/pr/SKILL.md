---
name: pr
description: Open a pull request for the current work the way this repo requires — branch off origin/main, body passed by file, rebase not merge, then merge once CI is green. Use when asked to open, raise, or push up a PR, to land or ship a change, or when work is finished and needs to go to main.
---

# Opening a PR here

Everything reaches `main` through a PR. There are four hard-won mechanics below; each of
them has already gone wrong at least once.

## Before anything else

**Never commit to `main`, and never fast-forward it.** If work is already sitting on local
`main`, do not push it: rewind and move it to a branch.

```powershell
git branch feat/<slug>            # keep the commits
git reset --hard origin/main      # put main back
git checkout feat/<slug>
```

**Branch off `origin/main`, not local `main`.** Always fetch first:

```powershell
git fetch origin
git checkout -b feat/<slug> origin/main
```

## Documentation is part of the PR, not a follow-up

Run the `/docs-sync` skill before opening. `CLAUDE.md` requires the docs to move with the code
in the same PR — a PR that leaves `docs/architecture.md` or `CLAUDE.md` asserting something
untrue is incomplete work, not a tidy-up for later.

## Verify, and quote real output

Run the test build and the suite, and read the actual summary:

```powershell
cmake --preset host
cmake --build --preset host
ctest --preset host
```

If the change touches CMake files, shared headers, `PicoDCCDisplay`, or anything behind
`#ifdef TEST_BUILD`, **also run the firmware build** (`cmake --build --preset pico`) — CI does
not cross-compile, so hardware-mode breakage reaches `main` unless you catch it here. It builds
into its own tree, so nothing needs clearing or restoring. The `/verify` agent does the full
sweep.

Never write a passing test count into a PR body without having run it in this session.

## Writing the body — pass it by file

**Never pass a multi-line body inline.** Backticks detonate in the shell, `>` silently creates
a junk file in the repo, and here-strings and heredocs both misbehave here. Write the body to
a file and pass the path:

```powershell
gh pr create --title "<title>" --body-file .git/pr-body.md
```

Put the file somewhere untracked — `.git/` is ideal, since nothing will ever stage it. Then
**check `git status --short` before any `git add`**, and never reach for `git add -A` with an
unexplained file in the tree. `build/` and `CircuitDesign/` are gitignored; if either shows up
staged, something is wrong.

Body shape — prose explaining the change, matching the repo's commit style rather than a form.
Read a recent one (`gh pr view <n>`) before writing:

- what the change does, and the problem it solves
- the design decisions worth knowing, and anything deliberately refused
- what is tested, with the real output
- **whether the hardware build was run**, and its result
- doc updates included
- `Closes #<n>` where it applies

## Never merge `main` into the branch

Merging `main` in breaks the rebase button. To take on upstream changes:

```powershell
git fetch origin
git rebase origin/main
```

## Merging

Paul does not review the diffs — PRs stand as change blocks, and **merging is yours once CI is
green**. Wait for the `CI` workflow to pass, then merge:

```powershell
gh pr checks <n> --watch
gh pr merge <n> --squash --delete-branch
```

Do not merge on a red or still-running CI. If CI fails, fix it on the branch and push — a
failing check is not something to explain away in a comment. Remember CI only proves the
**test** build: a green tick is not evidence the firmware still links.

Afterwards, `/branch-cleanup` removes the merged local branch and any stale worktree.

## Report

The PR URL, the title, whether CI passed, whether the hardware build was run, and whether you
merged it. If you stopped short of merging, say exactly why.
