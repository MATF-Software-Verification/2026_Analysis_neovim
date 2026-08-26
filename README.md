# Software Verification Analysis of Neovim v0.12.5

Course: *Verifikacija softvera*, Matematički fakultet, Univerzitet u Beogradu

## 1. Author info

Relja Pešić, index 1064/2024

## 2. Analyzed project description

[Neovim](https://github.com/neovim/neovim) is a ~374,000-line C codebase (plus ~146,000 lines
of Lua for runtime configuration, LSP, and Treesitter integration), built with CMake — a mature,
actively-maintained fork of Vim with a real existing CI pipeline of its own.

- **Branch/tag analyzed**: `v0.12.5` (latest stable patch release of the v0.12 series)
- **Commit pinned in the `neovim/` submodule**: `5885a30e1e1225349079e7a1c4a3848aa8e43e42`
- **Source**: added as a git submodule at [`neovim/`](neovim/), unmodified — [`custom.patch`](custom.patch)
  is empty because no changes were made to the analyzed source anywhere in this project.

Auditing the repository (see `ProjectAnalysisReport.md` §1 for the full writeup) found that
upstream's own CI already runs ASan/UBSan, TSan, CodeQL, and nightly Coverity on every commit —
but has **no code-coverage measurement, no fuzz-testing harness, and no formal/symbolic
verification anywhere**. That gap is what this project's six techniques target, plus two tools
picked specifically because they are **not** covered by the course's own exercises (course
materials checked at `../VS-materials/`): `cppcheck` (the course's static-analysis exercises use
the Clang Static Analyzer / `scan-build`, not `cppcheck`) and `semgrep` (no security-scanning
tool is covered in the course exercises at all).

## 3. Tools used

All six target the real, unmodified `neovim/src/nvim/...` source at the commit above. Five of
the six focus on `src/nvim/base64.c` — a small, self-contained, previously **0%-covered**
function that decodes attacker-shaped input (see report §1/§3) — so that unit testing, fuzzing,
and model checking can be directly compared on the same target; static analysis and security
scanning instead sweep the whole of `src/nvim` since they don't require a hand-built harness.

| # | Tool/technique | Category | Dir |
|---|---|---|---|
| 1 | Unit tests + code coverage (`lcov`/`gcov`) | Testing | [`unit_tests/`](unit_tests/) |
| 2 | Fuzz testing (LLVM `libFuzzer` + ASan/UBSan) | Fuzzing | [`fuzzing/`](fuzzing/) |
| 3 | Bounded model checking (`CBMC`) | Model checking | [`cbmc/`](cbmc/) |
| 4 | CPU profiling (`Valgrind`: `callgrind`) | Profiling | [`valgrind/`](valgrind/) |
| 5 | Static analysis (`cppcheck`) — *not covered in course* | Static analysis | [`cppcheck/`](cppcheck/) |
| 6 | Security scanning (`semgrep`) — *not covered in course* | Security | [`semgrep/`](semgrep/) |

(Tests and their coverage tool count as a single item per the course's rules; only one
Valgrind-family tool is used, per the course's cap.)

### 3.1 Unit tests + code coverage

**What**: hand-written unit tests for `base64_encode()`/`base64_decode()`
(`unit_tests/tests/test_base64.c`) — known-vector checks, an all-lengths-0..32 round trip, and
four rejection cases — built with `--coverage` and measured with `lcov`/`genhtml`.

**Setup**: a C compiler with gcov support, `lcov`/`genhtml`, and the `neovim` submodule
configured once (`cd neovim && cmake --preset default`).

**Reproduce**:
```sh
python3 unit_tests/run_tests.py
```
Full walkthrough: [`unit_tests/RunningTests.md`](unit_tests/RunningTests.md) /
[`.pdf`](unit_tests/RunningTests.pdf).

### 3.2 Fuzz testing

**What**: `fuzzing/harness.c` feeds raw bytes into `base64_decode()` and checks a
decode→encode→decode round-trip property, under `-fsanitize=fuzzer,address,undefined`.

**Setup**: a `clang` with a working `-fsanitize=fuzzer` runtime (on macOS, Apple's bundled clang
compiles the flag but lacks the runtime archive — install Homebrew LLVM:
`brew install llvm`), and the `neovim` submodule configured once.

**Reproduce**:
```sh
./fuzzing/run_fuzz.sh          # or: ./fuzzing/run_fuzz.sh <seconds-per-run>
```

### 3.3 Bounded model checking (CBMC)

**What**: `cbmc/harness_cbmc.c` marks a 12-byte buffer and its length fully nondeterministic and
calls `base64_decode()` then `base64_encode()`; CBMC either proves every reachable state safe for
*all* such inputs, or produces a counterexample.

**Setup**: `cbmc`, and the `neovim` submodule configured once.

**Reproduce**:
```sh
./cbmc/run_cbmc.sh
```

### 3.4 CPU profiling (Valgrind: callgrind)

**What**: `valgrind/workload.lua` runs a headless editing workload (buffer population, extmark
creation, a buffer-wide substitute, fuzzy matching); `callgrind` profiles it at the instruction
level.

**Setup**: Docker (Valgrind does not support macOS on arm64, or a Linux `perf`-based
alternative inside Docker Desktop's `linuxkit` kernel — see report §6 for why callgrind was kept
and `perf` was not).

**Reproduce**:
```sh
./valgrind/run_valgrind.sh
```

### 3.5 Static analysis (cppcheck)

**What**: `cppcheck` over all of `src/nvim`, driven by CMake's `compile_commands.json` so it sees
the project's real include paths/defines.

**Setup**: `cppcheck` (and the `neovim` submodule configured once, for `compile_commands.json`).

**Reproduce**:
```sh
./cppcheck/run_cppcheck.sh
```
Raw results: [`cppcheck/report/cppcheck.xml`](cppcheck/report/cppcheck.xml); categorized counts:
[`cppcheck/report/summary.txt`](cppcheck/report/summary.txt).

### 3.6 Security scanning (semgrep)

**What**: `semgrep` over all of `src/nvim` with the public `p/c`, `p/security-audit`, and
`p/cwe-top-25` rulesets — independent of upstream's own CodeQL (different engine, different
rule authors).

**Setup**: `semgrep`.

**Reproduce**:
```sh
./semgrep/run_semgrep.sh
```
Results: [`semgrep/report/semgrep.txt`](semgrep/report/semgrep.txt) /
[`semgrep.sarif`](semgrep/report/semgrep.sarif).

## 4. Conclusions

- **Coverage measurement** found a concrete gap upstream's own extensive test suite doesn't
  fill: `base64.c` sits at 0% line coverage from `test/unit`.
- **Writing our own unit tests** for that function closed the gap directly: 98.1% line / 100%
  function / 85.2% branch coverage, all 6 test functions passing.
- **Fuzzing** stress-tested the same function with tens of millions of adversarial inputs under
  ASan/UBSan — zero crashes, leaks, or round-trip violations.
- **CBMC** proved the complementary, exhaustive property: no memory-safety or arithmetic
  violation exists for *any* input up to 12 bytes (`VERIFICATION SUCCESSFUL`, 0/466 properties
  failed) — unit tests give confidence by example, fuzzing by adversarial sampling, CBMC by
  proof; together they're considerably stronger evidence than any one alone.
- **Callgrind profiling** of a bulk-substitute workload found a genuinely non-obvious hotspot:
  `memline.c`'s line-lookup hash map dominates instruction count, not the regex engine or the
  extmark tree most people would guess first.
- **cppcheck** (2,920 findings, mostly `style`) turned up 134 `error`-severity findings; a manual
  check of a representative one (`file_search.c:1188`, `uninitStructMember`) confirmed it as a
  false positive from the tool's limited flow-sensitivity across `if (!url)` guards — a useful
  reminder that static-analysis output needs verification, not blind trust.
- **semgrep** flagged 69 uses of `strcat`/`strcpy`/`strncpy` across 28 files (two rule
  categories) — pattern-based, so it can't reason about buffer sizing; a manual check of one
  (`fold.c:3253`) confirmed the destination buffer's size already accounts for the concatenated
  string, i.e. not currently exploitable, but exactly the kind of code that becomes a bug the
  next time someone edits the size computation without also updating the `strcat`.
- None of these eight findings (a coverage gap, a from-scratch test suite closing it, a clean
  fuzzing result, an exhaustive safety proof, a non-obvious profiling hotspot, and two
  independently-discovered static/security findings) existed before this analysis — despite
  upstream's own CI already running ASan/UBSan/TSan/CodeQL/Coverity on every commit.

Full narrative, configuration details, and interpretation for every tool:
[`ProjectAnalysisReport.md`](ProjectAnalysisReport.md).
