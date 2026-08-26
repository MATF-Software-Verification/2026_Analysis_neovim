# Project Analysis Report — Neovim v0.12.5

**Author**: Relja Pešić, index 1064/2024
**Project**: [Neovim](https://github.com/neovim/neovim)
**Branch/tag**: `v0.12.5`
**Commit pinned in `neovim/` submodule**: `5885a30e1e1225349079e7a1c4a3848aa8e43e42`

This is the deep-dive companion to `README.md`: for each of the six tools, what was run, why,
with what configuration, what the results actually showed, and what that means. `README.md`
carries the short version and the copy-pasteable reproduction commands.

## 1. Project selection and tool-selection rationale

Neovim is a ~374,000-line C codebase (plus ~146,000 lines of Lua for runtime configuration,
LSP, and Treesitter integration), built with CMake. It is mature and actively maintained, with a
genuinely strong existing CI pipeline (`.github/workflows/` in the upstream repo): every push is
built and tested under **ASan/UBSan** and **TSan**, scanned by **CodeQL**, and scanned nightly by
**Coverity**.

Auditing that pipeline before picking any tools turned up three verification categories that are
**completely absent** from it: no code-coverage measurement anywhere (no `lcov`/`gcov` flags in
any `Makefile`/`CMakeLists.txt`), no fuzz-testing harness of any kind, and no formal/symbolic
verification. Re-running ASan/UBSan/TSan/CodeQL on the same code would not have produced new
information; those three gaps are where four of this project's six techniques focus. The
remaining two — `cppcheck` and `semgrep` — were picked specifically to satisfy the course's
"at least two tools not covered by the exercises" requirement: the course's own static-analysis
exercises (`VS-materials/09_clang`) use the Clang Static Analyzer (`scan-build`/`--analyze`), not
`cppcheck`, and no security-scanning tool appears anywhere in the course materials.

**Not attempted, and why**: mock-object testing doesn't fit — neovim's C code doesn't expose the
kind of collaborator interfaces the course's mocking examples target. **KLEE** was judged
redundant with CBMC on the same target function (both are bounded symbolic/model-checking
techniques on `base64.c`; running both would not add a distinct perspective). **Dafny** has no
natural entry point into a 25-year-old existing C codebase. `perf` was considered for profiling
but is unusable in this environment (see §2). Two Valgrind-family tools tried during earlier
exploration, `massif` (heap profiling) and `memcheck` (leak checking), are **not** part of the
counted tool set — the course caps this category at one Valgrind tool — but their headline
findings are noted as an aside in §6.3 since they were genuinely informative and cost nothing
extra to mention.

## 2. Environment

Development happened on **macOS (Apple Silicon, arm64)**, which mattered in a few ways:

- Apple's bundled `clang` compiles `-fsanitize=fuzzer` but its command-line tools don't ship the
  `libclang_rt.fuzzer_osx.a` runtime, so linking fails. Fixed by installing full LLVM via
  Homebrew (`brew install llvm`) and using that `clang` instead.
- **Valgrind does not support macOS on arm64 at all**, and `perf` is a Linux-kernel feature,
  unusable on macOS in any form. Both would need a Linux environment; profiling here runs inside
  a **Docker Linux container** (Docker Desktop's VM has a real, if minimal, Linux kernel).
- Even inside Docker, `perf` turned out to be unusable: Docker Desktop's VM runs a custom
  `linuxkit` kernel for which no matching `linux-tools` package exists via `apt` — a genuine
  environment limitation, not a project issue. `callgrind` (inside Valgrind) was used instead.
- `cbmc`, `lcov`, `cppcheck`, and `semgrep` all installed and ran natively on macOS without
  issue.

Before any analysis, the FAQ's "compile and run it" requirement was satisfied: neovim v0.12.5
was configured with CMake+Ninja and built natively (`cmake --preset default`,
`cmake --build build`, inside `neovim/`), then smoke-tested (`nvim --version`, a headless
command, and interactive use).

## 3. Unit tests + code coverage (`lcov`/`gcov`)

**Motivation**: a first pass measured coverage of upstream's *own* `test/unit` suite (46 spec
files driving neovim's C code directly via LuaJIT FFI, built with a second, `--coverage`-flagged
build directory, captured with `lcov --capture`/`genhtml`). Result: 790 tests, 783 passed / 6
skipped / 1 failed (`vim_snprintf() positional arguments` — reproduces consistently on this
macOS/arm64 host, most likely a libc `snprintf` behavioral difference from the glibc/Linux
environment neovim's own CI runs on; unrelated to `--coverage`, not investigated further as it's
outside this analysis's scope). Whole-codebase coverage from that suite: **6.3%** lines / 12.5%
functions (243 source files) — expected, since `test/unit` only targets self-contained C modules
directly and the bulk of neovim's behavioral coverage comes from `test/functional` (524 files,
driving a real spawned `nvim` over RPC, not instrumented here — a much larger, slower pass, out
of scope). The important number: **`src/nvim/base64.c` — 0.0% line and function coverage.** A
small, self-contained function that decodes/encodes attacker-shaped input, with *zero* coverage
from upstream's own tests, is exactly the kind of gap this project's testing techniques target —
so `base64_encode()`/`base64_decode()` became the shared target for this section, §4, and §5.

**What was built** (`unit_tests/tests/test_base64.c`): no test framework is pulled in on
purpose — `base64.c` only calls `xmalloc()`/`xfree()` (stubbed the same way as the fuzzing and
CBMC harnesses) and exposes two pure, stateless functions, so a framework built for
classes/objects with setup/teardown (like the course's QtTest) is unnecessary complexity. The
test file uses a ~15-line `assert()`-style `CHECK()` macro and `main()` runner instead:

- `test_known_vectors` — encode/decode against the standard RFC 4648 examples
  (`"Man"` → `"TWFu"`, `"Ma"` → `"TWE="`, `"M"` → `"TQ=="`, `""` → `""`) — an oracle independent
  of the implementation, not just a round trip that could hide a symmetric encode/decode bug.
- `test_roundtrip_all_short_lengths` — every input length from 0 to 32 bytes, round-tripped
  through encode then decode, to exercise the 8-byte and 4-byte bulk-copy loops in
  `base64_encode()` plus all three tail-remainder branches (0, 1, or 2 leftover bytes).
- Four rejection tests: a length not a multiple of 4, an invalid alphabet character, a `=`
  placed where padding can't legally start, and a wrong padding-character count — the four ways
  `base64_decode()` reaches its `invalid:` exit path.

**Configuration**: compiled with `--coverage -O0`, linked, run, then `lcov --capture` over the
build directory followed by `lcov --extract '*/src/nvim/base64.c'` to scope the report to the
function under test (the raw capture also picks up whatever the compiler pulled in via headers),
rendered with `genhtml`.

**Result**: all 6 test functions pass (`unit_tests/test_run.log`). Coverage of `base64.c`:

| Metric | Result |
|---|---|
| Lines | 98.1% (106/108) |
| Functions | 100% (2/2) |
| Branches | 85.2% (46/54) |

Full report: `unit_tests/coverage_html/index.html`. **Interpretation**: from 0% to 98.1% line
coverage with under 100 lines of test code, because the target function is small, pure, and has
a fully specified interface — exactly the profile the FAQ's "is it easy to add unit tests?"
question is asking about. The remaining uncovered lines/branches are almost entirely
arithmetic-edge-case checks inside the `invalid:` path (e.g. `acc_len > 4` after certain bit
patterns) that are hard to reach without also triggering an earlier, coarser check first.

## 4. Fuzz testing (LLVM `libFuzzer`)

**Target**: the same `base64_decode()`/`base64_encode()`, chosen because it's genuinely
self-contained, has zero existing coverage (§3), and does exactly the "decode a length-prefixed,
attacker-shaped byte string" work fuzzing is built for.

**Harness** (`fuzzing/harness.c`): `LLVMFuzzerTestOneInput` feeds raw fuzzer bytes into
`base64_decode()`. If decoding succeeds, it checks a round-trip property: re-encoding the
decoded bytes and decoding that again must reproduce the original bytes exactly.

**Build**: `clang -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined`, linking
the harness directly against the real, unmodified `base64.c` (see `fuzzing/run_fuzz.sh`).

**Result**: two runs (`fuzzing/fuzz_run.log`) — no crashes, leaks, timeouts, or round-trip
violations. Coverage plateaus quickly at 50/50 edges (most of the function's branch space is
small: padding-length checks, alphabet lookups, invalid-character rejection). Corpus
minimization (`-merge=1`) reduces the corpus without losing coverage; minimized corpus:
`fuzzing/corpus_minimized/`.

**Interpretation**: for a function with zero prior test coverage, tens of millions of
adversarial-shaped inputs under memory/UB sanitizers found nothing. That's evidence *for*
correctness within the bounds fuzzing can reach — sampling, not proof — which is exactly the gap
§5 addresses.

## 5. Bounded model checking (CBMC)

Fuzzing samples inputs and can never prove the absence of a bug, only fail to find one in the
time given. CBMC does the complementary thing: for inputs up to a fixed size bound, it either
proves *every* reachable state safe, or produces a concrete counterexample.

**Harness** (`cbmc/harness_cbmc.c`): a `main()` that declares a 12-byte buffer, marks every byte
and the length nondeterministic (`nondet_char()`, `__CPROVER_assume(src_len <= 12)`), and calls
`base64_decode()` then `base64_encode()` on the result — the same real, unmodified `base64.c`.

**Command** (`cbmc/run_cbmc.sh`): `--bounds-check --pointer-check --signed-overflow-check
--unsigned-overflow-check --div-by-zero-check --unwind 14 --unwinding-assertions --trace`.

**Result**: `VERIFICATION SUCCESSFUL` — **0 of 466 checked properties failed**
(array-bounds, pointer-dereference-validity, signed/unsigned arithmetic overflow,
divide-by-zero, undefined-shift, and unwinding-sufficiency assertions), for *every* possible
input up to 12 bytes and every possible byte value in it. Full log: `cbmc/cbmc_output.log`.

**Interpretation**: combined with §3 and §4, this gives three independent, complementary
guarantees for the same previously-untested function: passing behavior on a curated set of known
cases and edge lengths (unit tests), no violation found across tens of millions of adversarial
inputs (fuzzing, broad but non-exhaustive), and no violation possible for *any* input up to 12
bytes (CBMC, exhaustive but bounded). None of the three existed before this analysis, since
`base64.c` had 0% test coverage to begin with.

## 6. CPU profiling (Valgrind: `callgrind`)

**Why callgrind, and why only one Valgrind tool**: the course caps this project at a single
Valgrind-family tool. `callgrind` was kept because, of the Valgrind tools tried while scoping
this project, it produced the most interesting, non-obvious result (below); `massif` and
`memcheck` were dropped from the counted tool set accordingly (their results are summarized
briefly in §6.3 as a footnote, not as a separate reproducible technique).

**Workload** (`valgrind/workload.lua`): a headless Lua script run via
`nvim --headless -u NONE -l workload.lua`, exercising four things in sequence: populating a large
buffer, creating tens of thousands of extmarks (exercising the extmark tree,
`src/nvim/marktree.c`), a buffer-wide substitute (`:%s/quick/QUICK/g` and back), and repeated
fuzzy-match queries over a candidate list (`vim.fn.matchfuzzy`, `src/nvim/fuzzy.c`). Size is
controlled by `$NVIM_PROFILE_SCALE` so the callgrind pass can use a much smaller workload than a
native timing run (instrumentation overhead is substantial).

### 6.1 Native baseline (macOS, RelWithDebInfo, scale=1.0)

200,000 lines, 50,000 extmarks, 20,000 fuzzy candidates × 20 queries:

| Phase | Time |
|---|---|
| Populate buffer | 0.059s |
| Create extmarks | 0.013s |
| Substitute (×2) | 0.344s |
| Fuzzy match (×20) | 0.079s |
| **Total** | **0.495s** |

The buffer-wide substitute dominates wall time (~70%) even though it's conceptually the
"simplest" operation — worth digging into with an instruction-level profiler.

### 6.2 Instruction-level hot path (`callgrind`, scale=0.02: 4,000 lines / 1,000 extmarks / 400
fuzzy candidates, built with `make` — including neovim's bundled third-party deps — and run
inside the `valgrind/Dockerfile` Ubuntu 24.04 container via `valgrind/run_valgrind.sh`)

127,056,883 total instructions. Top consumers (`valgrind/results/callgrind_annotate.txt`):

| % of instructions | Function |
|---|---|
| 12.24% + 2.69% | `map_key_impl.c.h: mh_find_bucket_int64_t` (two call sites) |
| 8.91% + 2.17% | `memline.c: ml_find_line` |
| 4.46% | `marktree.c: marktree_itr_get_ext` |
| 3.47% | `mbyte.c: utfc_ptr2len` |
| 2.48% + 2.27% | `map_key_impl.c.h: mh_delete_int64_t`, `mh_put_int64_t` |
| 2.30% | `regexp.c: vim_regsub_both` |
| 1.94% + 1.93% | `undo.c: u_savecommon`, `ex_cmds.c: do_sub` |

(Function names carry `.lto_priv.N`/`'N` suffixes because this build enables link-time
optimization; several are the same source function split across call sites or specialized by the
compiler.)

**This is the analysis's most non-obvious finding**: instruction time during a bulk substitute is
dominated by `mh_find_bucket_int64_t` / `ml_find_line` (>26% combined) — the hash-map lookup
neovim's `memline.c` uses to map a logical line number to its in-memory block — not by the regex
engine itself (`vim_regsub_both` alone is 2.30%, and no `nfa_regexec_both` frame even clears the
1% reporting threshold at this scale), and not by the extmark tree (`marktree_itr_get_ext` is
4.46%, present because substituting text also has to keep extmarks in sync, but clearly
secondary). The cost of "substitute across the whole buffer" in this workload is dominated by
*finding* each line, not by matching or rewriting it — consistent with the native timing in §6.1,
where the substitute phase alone was ~70% of total wall time (and, at this run's much smaller
scale, `substitute=0.708s` out of `total=0.815s` — the same lopsided split holds).

### 6.3 Aside: massif/memcheck (not part of the counted 6 tools; kept for context)

Tried while scoping this project, before the one-Valgrind-tool rule narrowed it to callgrind
alone. `massif` (heap profiling, scale=0.2) showed a peak heap of ~24.1MB, ~21% of which was
`undo.c: u_save_line_buf` copying every touched line into the undo tree *before* applying a
change — expected, correct behavior, but a real quantified cost most people wouldn't guess.
`memcheck --leak-check=full` (scale=0.05) found one "definitely lost" allocation tracing entirely
through LuaJIT's own JIT trace-compilation machinery (`lj_mcode_reserve` → unwind-frame metadata
registration) — a known, benign, one-time artifact of embedding LuaJIT's JIT, not a defect in
neovim's own C code.

## 7. Static analysis (`cppcheck`)

**Why cppcheck**: not covered by the course exercises (`09_clang` covers the Clang Static
Analyzer via `scan-build`/`--analyze`, a different engine with different checkers) — one of the
two tools satisfying the course's independent-discovery requirement.

**Configuration** (`cppcheck/run_cppcheck.sh`): driven by CMake's `compile_commands.json`
(`--project=`) so cppcheck sees the project's real include paths and preprocessor defines instead
of guessing them, filtered to `src/nvim` only, `--enable=warning,style,performance,portability`.

**Result**: 2,920 findings total over 372 files (`cppcheck/report/cppcheck.xml`; categorized
counts in `cppcheck/report/summary.txt`):

| Severity | Count |
|---|---|
| style | 2,754 |
| error | 134 |
| warning | 27 |
| portability | 5 |

The `style` bucket is dominated by `constVariablePointer`/`constParameterPointer`
(missing-`const` suggestions, 1,546 combined) and `badBitmaskCheck` (604) — real but low-severity
style opinions, not correctness bugs. The `error`-severity bucket is more interesting:
`uninitStructMember` (116), `zerodiv` (6), `uninitvar`/`legacyUninitvar` (6), plus a handful of
`internalAstError`/`syntaxError` (cppcheck's own parser giving up on complex macro-heavy code,
not a code defect).

**Manual verification of a representative finding**: `file_search.c:1188`,
`uninitStructMember` on `file_id.inode`/`file_id.device_id`. Reading the surrounding code shows
`file_id` is populated by `os_fileid(fname, &file_id)` earlier in the function, gated by the same
`if (!url)` condition that gates both the read at line ~1179 and the write cppcheck flagged —
cppcheck's dataflow analysis doesn't track that the two `!url` branches are the same condition,
so it can't see that `file_id` is always initialized before this use. **Confirmed false
positive**, not a real bug. This is included specifically because a static-analysis section that
only reports raw counts without checking whether the tool's claims hold up isn't actually
verification — the same caution applies to the other 133 `error`-severity findings, which were
not individually re-verified given the scope of this project.

## 8. Security scanning (`semgrep`)

**Why semgrep**: no security-scanning tool is covered anywhere in the course exercises — the
second tool satisfying the independent-discovery requirement, and deliberately independent of
what upstream's own CodeQL already runs on every commit (different engine, different rule
authors, so it can surface things CodeQL's own ruleset doesn't check for).

**Configuration** (`semgrep/run_semgrep.sh`): the public `p/c`, `p/security-audit`, and
`p/cwe-top-25` rulesets over all of `src/nvim`.

**Result**: 69 findings (`semgrep/report/semgrep.txt`, `semgrep/report/semgrep.sarif`), all from
two rule categories: `insecure-use-strcat-fn` (18 files) and `insecure-use-string-copy-fn` —
`strcpy`/`strncpy` (10 files) — pattern-based warnings against any use of these functions,
regardless of whether the destination buffer is actually big enough.

**Manual verification of a representative finding**: `fold.c:3253`, `strcat(r, s)`. Reading
backward, `r` is allocated a few lines earlier with
`len = strlen(txt) + strlen(dashes) + 20 + strlen(s)` — the length of `s` (the string about to be
`strcat`'d) is already included in the allocation size. **Not currently exploitable** — but this
is exactly the kind of code that becomes a real buffer overflow the next time someone edits the
size computation without also updating the matching `strcat` call, which is why the pattern is
worth flagging even where today's specific instance is safe. As with §7, the other findings were
not individually re-verified given this project's scope; the value here is in the class of risk
surfaced (unchecked-by-construction string concatenation, invisible to a type-safe language and
to CodeQL's own default C ruleset in this case) rather than a claim that all 69 sites are live
bugs.

## 9. Conclusion

The six techniques reinforce each other rather than standing alone, and land on one previously
completely-unverified function from three independent angles plus two whole-codebase sweeps that
were deliberately chosen to be outside what the course already taught:

1. **Coverage measurement** found a concrete, quantified gap upstream's own extensive test suite
   doesn't fill: `base64.c`, 0% covered.
2. **Writing unit tests for that gap** closed it directly and cheaply: 98.1% line coverage from
   under 100 lines of test code, because the target is small and pure.
3. **Fuzzing** filled the same gap with adversarial breadth — tens of millions of inputs, zero
   findings.
4. **CBMC** filled it with exhaustive depth — a proof of memory- and arithmetic-safety for all
   small inputs, a stronger guarantee than sampling can ever give for the sizes it covers.
5. **Profiling** answered a different question entirely — not "is this code correct?" but "where
   does its time actually go?" — and the answer (memline's line-lookup hash map dominates
   instructions during a bulk substitute; the regex engine most people would guess first is
   comparatively cheap) was genuinely counter-intuitive going in.
6. **Static analysis and security scanning**, picked specifically because the course didn't teach
   them, surfaced real classes of findings (a flow-insensitivity false positive worth
   understanding, and a not-yet-exploitable-but-fragile string-concatenation pattern) that
   neither of the course's own covered tools (Clang Static Analyzer) nor upstream's CodeQL had
   flagged in the specific instances checked here.

None of these six results existed in neovim's own considerable CI investment before this
analysis, despite that CI already running ASan/UBSan/TSan/CodeQL/Coverity on every commit.

## Reproducing this analysis

Tool versions used: CMake 4.4.2, Ninja 1.13.2, `lcov`/`genhtml` 2.5-0, CBMC 6.11.0, Homebrew
LLVM/clang 22.1.8 (fuzzing), Valgrind 3.22.0 (inside `ubuntu:24.04`, Docker Desktop for Mac),
cppcheck 2.21.0, semgrep 1.174.0.

See `README.md` §3 for the exact reproduction command for each tool; every tool's directory
(`unit_tests/`, `fuzzing/`, `cbmc/`, `valgrind/`, `cppcheck/`, `semgrep/`) contains its own
script and raw output.
