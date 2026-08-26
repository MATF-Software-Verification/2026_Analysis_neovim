---
name: vs-seminar-project
description: Guide building or continuing a MATF "Software Verification" (VS) seminar project — picking/validating the analyzed open-source project, selecting 6+ verification/testing tools under the course's counting rules, laying out the mandatory repo structure (submodule, per-tool dirs, unit_tests, CI), and writing README.md / ProjectAnalysisReport.md. Use when the user asks to start, continue, restructure, or review a VS seminar project, add a verification tool to one, set up the course CI, or check whether the repo/README satisfies the course requirements. Triggers on "VS seminar project", "software verification project", "MATF verification project", "VS-project-ci", "ProjectAnalysisReport".
---

# MATF Software Verification — Seminar Project Builder

Builds a practical seminar project for the *Verifikacija softvera* course: apply
verification/testing tools to one open-source project and produce a repo + reports the
student can defend live, unassisted.

**Hard rule, overrides everything else below:** never commit or write up a script, config,
report section, or tool output the student has not personally reviewed and can explain —
what it does, why that tool/parameter was picked, what the output means. If a part can't be
explained at defense time, the whole analysis must be redone on a different project, with the
max achievable score cut by 10 points. After generating any artifact, give a short
plain-language explanation of it and get the student's confirmation before moving on to the
next one — don't batch this up for the end.

## References (fetch these, don't guess their contents)

- FAQ (selection & common issues): https://github.com/MATF-Software-Verification/VS-analysis-project-faq/blob/main/general.md
- Repo structure example: https://github.com/MATF-Software-Verification/VS-analysis-project-faq/blob/main/structure.md
- CI template to install: https://github.com/MATF-Software-Verification/VS-project-ci
- Tracking spreadsheet of already-analyzed projects: course-provided Google Sheet (ask the
  student for the link if reusing a past student project)

## Step 1 — Choose and validate the target project

- Must be open source. **Prefer established (non-student) projects** — course recommendation.
- If reusing a past student project: must be from an earlier generation, not the student's
  own, and not already analyzed before — check the tracking spreadsheet first.
- Before committing: **verify it actually builds and runs**. Reject it if it doesn't compile
  cleanly or is illegible/unmodular — don't take the student's word for it, build it yourself
  in this session.
- Score the fit against the FAQ questions before locking it in:
  - Existing tests? Easy to add unit tests? Logic separated from GUI/framework code?
  - Is coverage tracking feasible/worthwhile?
  - What lint/style tools apply to this language?
  - Does profiling make sense — CPU-bound work vs. a GUI event loop vs. a VM runtime needing
    something like async-profiler/JFR?
  - Does memory profiling make sense — leaks, RSS over time, massif graphs, heap dumps?
  - Is there user-input parsing worth fuzzing?
  - Any part isolable for symbolic execution / model checking?
  - Any bug here security-relevant?
  - Network traffic worth profiling?
- Let the language/toolchain the student actually wants to work in drive tool choice, not the
  other way around.
- If this repo already has an analyzed project (an existing submodule, README naming a
  target), treat Step 1 as **already resolved** — confirm it still builds before doing
  anything else, don't re-litigate the choice.

## Step 2 — Select ≥6 tools/techniques, respecting the counting rules

Count carefully — these rules are graded literally:

- **Tests are one item total**, regardless of framework — EXCEPT distinct test *categories*
  (unit vs. integration) each count separately.
- Tests included ⇒ **must** track code coverage with some tool. Coverage without tests doesn't
  count as a technique on its own. The coverage tool itself doesn't count as a separate
  item — it's testing support, bundled with the tests item.
- **Max 1 Valgrind-family tool total** — pick one of memcheck / callgrind / massif / helgrind,
  not several. Using two or more Valgrind tools (e.g. callgrind *and* massif *and* memcheck)
  is a rules violation, not "three tools" — collapse them to one before counting.
- **Max 1 formatting/style tool** (e.g. clang-format *or* clang-tidy, not both) counts toward
  the total.
- **At least 2 tools must NOT be ones covered in the course exercises** — check the course
  materials for what was already taught before picking, and be able to name which 2+ are the
  independent picks.
- Draw from: unit/integration testing + coverage, static analysis, dynamic analysis (Valgrind
  family), fuzzing, symbolic execution (KLEE), model checking (CBMC), profiling (CPU/memory/
  network), style/lint, security scanning.
- Every tool needs a script or documented command sequence in the repo that reproduces its
  results from scratch — no tool is "done" until that reproduction path exists and has been
  run clean.
- Before writing any code: list out the exact tool set against these rules explicitly (which
  is the 1 Valgrind tool, which 2+ are outside the course, whether tests+coverage are counted
  as one bundle) and confirm the count is ≥6. If it's short, or double-counts a Valgrind/style
  tool, fix the plan before building anything.

## Step 3 — Repository structure (mandatory, exact layout)

```
.
├── .github
│   └── workflows                  # from VS-project-ci — gate.yml, tickets.yml
│       ├── gate.yml
│       └── tickets.yml
├── <project-name>                 # git submodule of the analyzed project, pinned to a commit
├── custom.patch                   # any modifications made to the original project's source
├── unit_tests/
│   ├── run_tests.py
│   ├── RunningTests.md
│   ├── RunningTests.pdf
│   └── tests/
│       ├── MyUnitTests1.cpp
│       └── ...
├── <tool-1>/                      # one directory per tool (klee, cbmc, valgrind, cppcheck, ...)
│   ├── run_<tool>.sh
│   └── <tool-output-artifacts>    # logs, reports, counterexamples, graphs, screenshots
├── <tool-2>/
│   └── ...
├── .gitignore
├── .gitmodules
├── README.md
├── ProjectAnalysisReport.md       # or .pdf
└── ProjectAnalysisReport.pdf      # if not markdown
```

- The analyzed project is a **git submodule** (`git submodule add <url> <project-name>`),
  pinned to a specific commit on a specific branch — not a copy, not a subtree.
- **Every tool gets its own top-level directory** holding both its raw output and the
  script(s) that reproduce it. Don't nest multiple tools under one directory (e.g. don't put
  callgrind/massif/memcheck output in one shared `valgrind/` dir unless the course allows a
  single directory for the single Valgrind-family tool actually chosen).
- Repos that deviate from this template, or omit required README content, are rejected
  outright — treat this structure as non-negotiable, not a starting suggestion.
- If the repo currently has ad-hoc directories (e.g. a flat `artifacts/<tool>/` layout, no
  submodule, no `unit_tests/`, no CI), that's a **gap against the mandatory structure**, not a
  valid alternative — flag it explicitly and migrate before considering the project done.

## Step 4 — CI configuration

- Install `.github/workflows/gate.yml` and `tickets.yml` from
  https://github.com/MATF-Software-Verification/VS-project-ci. Mandatory for every submitted
  repo.
- Verify it's present **and passing** (not just added) before considering the project done —
  check the Actions tab / run status, don't just check the files exist.

## Step 5 — README.md (exactly these sections, no more, no less)

1. **Author info** — student name/identifying info per course requirements.
2. **Analyzed project description** — what it is, link to its source repo, branch analyzed,
   exact commit hash pinned in the submodule.
3. **Tools used** — every tool/technique from Step 2, each with clear step-by-step
   reproduction instructions (commands, scripts, setup/dependencies).
4. **Conclusions** — summary list of findings/takeaways across all tools.

## Step 6 — ProjectAnalysisReport.md (or .pdf)

- Detailed narrative, per tool: what was run, why that tool/config, what the results showed,
  what was concluded.
- Same project reference (link, branch, commit hash) as the README.
- This is the deep-dive companion to the README's summary — don't just duplicate the README
  here; the README is the index, this is the substance.

## Step 7 — Defense readiness gate (before calling it "done")

Walk every script, config file, report section, and tool output in the repo and confirm the
student can explain: what it does, why that tool/parameter was chosen, what the output means.
AI assistance in generating code/scripts/reports is fine — **anything not understood doesn't
get committed**. Do this incrementally as artifacts are produced, not as one final review:
after each artifact, give the plain-language explanation and get explicit confirmation before
moving to the next tool.

## Workflow notes

- Treat Steps 1–2 as a planning gate: don't start writing scripts or restructuring the repo
  until the tool list is confirmed to satisfy every counting rule in Step 2.
- Treat Step 7 as a running gate, not a final one: pause after each tool's artifacts are
  generated to explain and confirm before starting the next tool.
- When something in the repo conflicts with the mandatory structure (Step 3) or README
  contents (Step 5), say so plainly and propose the migration — don't silently work around it
  or leave the deviation in place.
