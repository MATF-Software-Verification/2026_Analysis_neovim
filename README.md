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
tool is covered in the course exercises at all). A seventh, **bonus** tool — Coverity — is
documented in §3.7; it is not one of the six and not counted toward the course's requirements,
since it's the same tool upstream already runs nightly (see §3.7 for why it was run locally
anyway).

## 3. Tools used

All six target the real, unmodified `neovim/src/...` source at the commit above. Unit testing,
fuzzing, and model checking are each run against **two** independently-chosen, self-contained
targets so the three techniques can be directly compared on the same code: `src/nvim/base64.c`
(previously **0%-covered**, see report §1/§3) and the **msgpack-rpc wire format**
(`src/nvim/msgpack_rpc/{un,}packer.c` + the vendored `src/mpack/` tokenizer) — chosen after
`base64.c` alone left the project's own testing/fuzzing/model-checking coverage limited to a
single function, and picked specifically because it parses untrusted bytes off an RPC socket, the
same "attacker-shaped input" profile as `base64.c`. **CBMC found a real bug there** — a genuine
integer-underflow/out-of-bounds-read in `unpack_string()`, confirmed with a standalone ASan
reproduction — see report §5a for the full writeup; it's the strongest single result in this
project. Static analysis and security scanning sweep the whole of `src/nvim` **and** `src/mpack`
since they don't require a hand-built harness.

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

**What**: hand-written unit tests for two targets, both built with `--coverage` and measured with
`lcov`/`genhtml`:
- `unit_tests/tests/test_base64.c` — `base64_encode()`/`base64_decode()`: known-vector checks, an
  all-lengths-0..32 round trip, and four rejection cases.
- `unit_tests/tests/test_msgpack.c` — the msgpack-rpc decode/encode primitives: known byte
  sequences from the MessagePack spec, an integer/string encoding-width boundary sweep, and four
  rejection cases.

**Setup**: a C compiler with gcov support, `lcov`/`genhtml`, and the `neovim` submodule
configured once (`cd neovim && cmake --preset default`). The msgpack target additionally needs
libuv and luajit headers (already required to build the `neovim` submodule itself).

**Reproduce**:
```sh
python3 unit_tests/run_tests.py
```
Full walkthrough: [`unit_tests/RunningTests.md`](unit_tests/RunningTests.md) /
[`.pdf`](unit_tests/RunningTests.pdf).

### 3.2 Fuzz testing

**What**: four complementary harnesses, all under `-fsanitize=fuzzer,address,undefined`, two per
target:
- `fuzzing/harness.c` (decode-first) / `harness_encode.c` (encode-first) — `base64_decode()`/
  `base64_encode()`, as before.
- `fuzzing/harness_msgpack.c` (decode-first) / `harness_msgpack_encode.c` (encode-first) — the
  msgpack-rpc decode/encode primitives, same decode-first/encode-first split for the same reason
  (decode-first stresses rejection on mostly-malformed bytes; encode-first checks a
  fidelity round trip on always-well-formed ones).

**Setup**: a `clang` with a working `-fsanitize=fuzzer` runtime (on macOS, Apple's bundled clang
compiles the flag but lacks the runtime archive — install Homebrew LLVM:
`brew install llvm`), and the `neovim` submodule configured once.

**Reproduce**:
```sh
./fuzzing/run_fuzz.sh          # or: ./fuzzing/run_fuzz.sh <seconds-per-run>
```

### 3.3 Bounded model checking (CBMC)

**What**: two harnesses. `cbmc/harness_cbmc.c` marks a 12-byte buffer and its length fully
nondeterministic and calls `base64_decode()` then `base64_encode()`. `cbmc/harness_msgpack_cbmc.c`
does the same for `unpack_integer()`/`unpack_string()`/`unpack_array()`. CBMC either proves every
reachable state safe for *all* such inputs, or produces a counterexample — and for the msgpack
target, **it found a real one**: an integer-underflow in `unpack_string()` (see report §5a).

**Setup**: `cbmc`, and the `neovim` submodule configured once. The msgpack harness additionally
needs libuv/luajit headers and a small local shim (`cbmc/stub_headers/dirent.h`) that works around
a CBMC parser limitation on macOS — see that file's comment.

**Reproduce**:
```sh
./cbmc/run_cbmc.sh
```
The msgpack run is *expected* to end in `VERIFICATION FAILED` — that's the documented finding, not
a broken script. `cbmc/finding_unpack_string_underflow_repro.c` is a standalone, CBMC-independent
reproduction of the same bug (build with `-fsanitize=address` and run it directly).

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

**What**: `cppcheck` over all of `src/nvim` and `src/mpack` (the latter added alongside the
msgpack-rpc target, §3.1–3.3), driven by CMake's `compile_commands.json` so it sees the project's
real include paths/defines.

**Setup**: `cppcheck` (and the `neovim` submodule configured once, for `compile_commands.json`).

**Reproduce**:
```sh
./cppcheck/run_cppcheck.sh
```
Raw results: [`cppcheck/report/cppcheck.xml`](cppcheck/report/cppcheck.xml); categorized counts:
[`cppcheck/report/summary.txt`](cppcheck/report/summary.txt).

### 3.6 Security scanning (semgrep)

**What**: `semgrep` over all of `src/nvim` and `src/mpack` with the public `p/c`,
`p/security-audit`, and `p/cwe-top-25` rulesets — independent of upstream's own CodeQL (different
engine, different rule authors).

**Setup**: `semgrep`.

**Reproduce**:
```sh
./semgrep/run_semgrep.sh
```
Results: [`semgrep/report/semgrep.txt`](semgrep/report/semgrep.txt) /
[`semgrep.sarif`](semgrep/report/semgrep.sarif).

### 3.7 Bonus: Coverity (industrial static analysis)

**What**: `cov-analyze --all` (Synopsys/Black Duck Coverity Static Analysis 2026.3.2) over all of
`src/nvim`, run entirely locally — no upload to a Coverity Connect server. **Not** one of the six
required techniques: upstream already runs Coverity nightly (§2). Run anyway because institutional
access became available mid-project and because upstream's nightly results aren't public, so a
local run is genuinely new, reproducible data — mainly to see how setting up a commercial/
industrial static analyzer actually works, in contrast to the free tools above.

**Setup**: a *licensed* `cov-analysis` install (`license.dat` in `<install>/bin/` — unlike every
other tool in this project, this one cannot be freely reproduced without an active Coverity
license), a one-time compiler registration (`cov-configure --comptype clangcc --compiler
/usr/bin/cc -- <neovim's exact required compiler flags>` — see report for why the `--clang`
template alone wasn't enough), and the `neovim` submodule configured **and fully built** at least
once (capture replays `compile_commands.json` through `cov-translate` directly rather than
wrapping the build with `cov-build`, to avoid a macOS Xcode.app requirement `cov-build` has no way
around — see report for the full story, including a header-regeneration gotcha this order avoids).

**Reproduce**:
```sh
./coverity/run_coverity.sh
```
Summary (verbatim `cov-analyze` output): [`coverity/report/summary.txt`](coverity/report/summary.txt);
`base64.c`/`lua/base64.c`-specific findings (empty):
[`coverity/report/base64_findings.json`](coverity/report/base64_findings.json).

## 4. Conclusions

- **Coverage measurement** found a concrete gap upstream's own extensive test suite doesn't
  fill: `base64.c` sits at 0% line coverage from `test/unit`.
- **Writing our own unit tests** for that function closed the gap directly: 98.1% line / 100%
  function / 85.2% branch coverage, all 6 test functions passing. A second target (msgpack-rpc's
  decode/encode primitives) got its own test file too — 13.1%/20.7%/8.4%, much lower, but a
  direct, honest consequence of that target spanning far more deliberately-out-of-scope code than
  `base64.c` does (report §3/§1.1), not weaker testing of the functions actually targeted.
- **Fuzzing** stress-tested both targets with tens of millions of adversarial inputs under
  ASan/UBSan — zero crashes, leaks, or round-trip violations in either.
- **CBMC** proved the complementary, exhaustive property on `base64.c`: no memory-safety or
  arithmetic violation exists for *any* input up to 12 bytes (`VERIFICATION SUCCESSFUL`, 0/464
  properties failed). **On the msgpack-rpc target, it found a real bug**: an integer-underflow in
  `unpack_string()` that produces an out-of-bounds read, confirmed independently with a
  standalone ASan reproduction and reachable via a crafted `.shada` file (report §5a) — the
  strongest single result in this project, and neither cppcheck, semgrep, nor fuzzing (in the
  time budget run here) caught it.
- **Callgrind profiling** of a bulk-substitute workload found a genuinely non-obvious hotspot:
  `memline.c`'s line-lookup hash map dominates instruction count, not the regex engine or the
  extmark tree most people would guess first.
- **cppcheck** (2,982 findings over `src/nvim` + `src/mpack`, mostly `style`) turned up 135
  `error`-severity findings; a manual check of a representative one (`file_search.c:1188`,
  `uninitStructMember`) confirmed it as a false positive from the tool's limited
  flow-sensitivity across `if (!url)` guards — a useful reminder that static-analysis output
  needs verification, not blind trust. (Checked specifically: nothing near the `unpack_string()`
  bug CBMC found.)
- **semgrep** flagged 69 uses of `strcat`/`strcpy`/`strncpy` across 28 files (two rule
  categories), unchanged by also scanning `src/mpack` (zero findings there) — pattern-based, so
  it can't reason about buffer sizing; a manual check of one (`fold.c:3253`) confirmed the
  destination buffer's size already accounts for the concatenated string, i.e. not currently
  exploitable, but exactly the kind of code that becomes a bug the next time someone edits the
  size computation without also updating the `strcat`.
- None of these findings (a coverage gap and a test suite closing it on two targets, clean
  fuzzing results, an exhaustive safety proof on one target *and a real bug found on the other*,
  a non-obvious profiling hotspot, and two independently-discovered static/security findings)
  existed before this analysis — despite upstream's own CI already running
  ASan/UBSan/TSan/CodeQL/Coverity on every commit.
- **Bonus — Coverity** (industrial static analysis, not one of the six): 899 defect occurrences
  across `src/nvim` with every checker enabled, but **zero** in `base64.c`/`lua/base64.c` — the
  same function the unit-test/fuzzing/CBMC trio above targets — reinforcing that these techniques
  catch genuinely different classes of issues rather than overlapping. (Not re-run against the
  msgpack-rpc target — report's "Reproducing this analysis" section notes why.)

Full narrative, configuration details, and interpretation for every tool:
[`ProjectAnalysisReport.md`](ProjectAnalysisReport.md).
