---
name: docs-sync
description: Bring PicoDCC's documentation into line with the code on the current branch, before the PR opens. Use after landing a change and before opening a PR, or when asked whether the docs still match the code, or whether a change falsifies CLAUDE.md or docs/architecture.md.
---

# Doc sync

`CLAUDE.md` requires documentation to move **with** the code, in the same PR — never as a
follow-up. This skill is that pass. It is not a summary of the diff: it is a check of four
specific places, each of which the diff may have falsified.

Work from the branch diff, not from memory of what you did:

```powershell
git fetch origin
git diff origin/main...HEAD --stat
git diff origin/main...HEAD
```

## The four places, in order

**1. `CLAUDE.md` — and only what the diff actually falsified.**

This file loads into **every session and every subagent**, so every line is paid for by tasks
that never touch the area. Keep it minimal. Check specifically:

- The **non-negotiable rules** — did this change add a new class of foot-gun, or make one of
  the existing rules wrong? A new rule earns its place only if breaking it causes a hard fault,
  a runaway loco, or a JMRI desync.
- The **component table** — new component, or a component whose ownership moved.
- The **transport facts** — new command form, changed UART or stdio wiring, changed
  emergency-stop or protocol behaviour. These are the facts people get wrong from memory.
- The **build commands** and the **test count** (currently 9 suites / 113 tests). Adding or
  removing a suite falsifies this file and `docs/architecture.md` together.

Reasoning belongs in `docs/`, not here. One line pointing at the doc.

**2. `docs/architecture.md` — the design record.**

Update when component responsibilities change, when work moves between Core 0 and Core 1, when
a new subsystem lands, or when the queue/priority model changes. Keep the mermaid diagram
consistent with the prose — a diagram showing the old core split is worse than no diagram.
Move implemented items out of any "Future Enhancements" section rather than leaving them in
both places. Update the per-component test counts.

**3. `docs/README.md` — the index and status header.**

New doc files must be indexed here or nobody finds them. Check the status header and the phase
tables: did this change complete a phase, close a prerequisite checkbox, or move something from
planned to implemented? The header currently carries a stale October 2025 date — if your change
touches the area it describes, refresh that section rather than adding to the staleness.

**4. The topic doc for the area you touched.**

`docs/service-mode-programming-plan.md`, `docs/dccex-compliance-analysis.md`,
`docs/dccex-jmri-compatibility-todos.md`, `docs/safety-recommendations.md`,
`docs/gpio-pinout-reference.md`, `docs/coverage-quick-start.md`,
`docs/hardware-test-quick-reference.md`. If the diff changes a command's behaviour, the
compliance and JMRI-compatibility docs are the ones that lie to the next reader first.

## Rules

- **Rewrite, never append.** When a later change supersedes an earlier one, rewrite the entry.
  The failure mode this repo already has is documents that read as a changelog with the current
  truth buried at the bottom.
- **Present tense, no history.** These describe the system as it is. "Now uses" and "was changed
  to" both make the reader work out what is true today.
- **Do not invent an entry to fill a section.** Most changes touch one or two of the four
  places. Say which you checked and found already correct.
- **Do not fix unrelated staleness.** This repo has plenty of it (see the October 2025 headers).
  Fixing it wholesale belongs in its own PR, not smuggled into a feature diff. Note it instead.
- **`.github/copilot-instructions.md` is a pointer, not a rules file.** If a rule changed, it
  changed in `CLAUDE.md`. Never reintroduce rules into the Copilot file.

## Output

A short list: file, what you changed, and why the diff required it. Then, explicitly, the
places you checked and left alone, and any staleness you noticed but deliberately left for a
separate PR.
