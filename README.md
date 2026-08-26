# Software Verification Analysis of Neovim v0.12.5

Course: *Verifikacija softvera*, Matematički fakultet, Univerzitet u Beogradu
Target project: [Neovim](https://github.com/neovim/neovim), tag **v0.12.5** (latest stable
patch release of the v0.12 series)

## 1. Why Neovim, and why these four tools

Neovim is a ~374,000-line C codebase (plus ~146,000 lines of Lua for runtime
configuration, LSP, and Treesitter integration), built with CMake. It is a mature,
actively-maintained project with a genuinely strong existing CI pipeline
(`.github/workflows/`): every push is built and tested under **ASan/UBSan** and **TSan**,
scanned by **CodeQL**, and scanned nightly by **Coverity**. Re-running any of those on the
same code would not have produced new information.

Auditing the repository turned up three verification categories that are
**completely absent** from upstream's own pipeline: there is no code-coverage
measurement anywhere (no `lcov`/`gcov`/coverage flags in `Makefile` or `CMakeLists.txt`),
no fuzz-testing harness of any kind, and no formal/symbolic verification. That gap is
where this analysis focuses, using tools from the course:

1. **Code coverage** (`lcov`/`gcov`) — measure what upstream's own `test/unit` suite
   actually exercises.
2. **Fuzz testing** (LLVM `libFuzzer` + ASan/UBSan) — stress-test an untrusted-input
   parser the coverage pass shows is completely untested.
3. **Bounded model checking** (`CBMC`) — exhaustively verify memory-safety properties
   of that same function for all inputs up to a size bound, as a complement to fuzzing's
   *sampling*.
4. **Profiling** (`Valgrind`: callgrind, massif, memcheck) — find hot paths and heap
   behavior in a representative editing workload.

All four target **the real, unmodified v0.12.5 source** (`neovim/src/nvim/...`); nothing
in the analyzed code was changed. Supporting files (harnesses, Docker setup, workload
scripts, raw tool output) live under [`artifacts/`](artifacts/).

Not attempted, and why: **mock-object testing** doesn't fit — neovim's C code doesn't
expose the kind of collaborator interfaces the course's mocking examples target.
**KLEE** was judged redundant with CBMC on the same target function (both are bounded
symbolic/model-checking techniques; running both on `base64.c` would not have added a
distinct perspective). **Dafny** has no natural entry point into a 25-year-old existing C
codebase. **gdb** was kept in reserve for triaging any fuzzer crash, but the fuzzer found
none to triage.

## 2. Environment and a build-environment finding

Development happened on **macOS (Apple Silicon, arm64)**, which turned out to matter:

- Apple's bundled `clang` compiles `-fsanitize=fuzzer` but its command-line tools
  don't ship the `libclang_rt.fuzzer_osx.a` runtime, so linking fails. Fixed by installing
  full LLVM via Homebrew (`brew install llvm`) and using that `clang` instead.
- **Valgrind does not support macOS on arm64 at all**, and **`perf` is a Linux-kernel
  feature**, unusable on macOS in any form. Both were run inside a **Docker Linux
  container** instead (Docker Desktop's VM has a real, if minimal, Linux kernel).
- Even inside Docker, `perf` turned out to be unusable: Docker Desktop's VM runs a custom
  `linuxkit` kernel (`6.12.76-linuxkit`) for which no matching `linux-tools` package
  exists via `apt`. This is a genuine environment limitation, not a project issue — noted
  and worked around by relying on Valgrind (callgrind/massif/memcheck) for the profiling
  section instead of the originally-planned perf comparison.
- `cbmc` and `lcov` installed and ran natively on macOS without issue.

Before any analysis, the FAQ's "compile and run it" requirement was satisfied: neovim
v0.12.5 was configured with CMake+Ninja, built natively (`cmake --preset default`,
`cmake --build build`), and smoke-tested (`nvim --version`, a headless command, and
interactive use).

## 3. Code coverage (`lcov`/`gcov`)

**Setup**: a second build directory (`build-coverage`) was configured with
`-DCMAKE_C_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage`, built, and run against
upstream's own `test/unit` suite (`cmake --build build-coverage --target unittest`, 46
spec files driving neovim's C code directly via LuaJIT FFI). Results were captured with
`lcov --capture` and rendered with `genhtml`.

**Test suite result**: 790 tests ran; **783 passed, 6 skipped, 1 failed**. The one
failure (`vim_snprintf() positional arguments`, `test/unit/testutil.lua:777`) is a
mismatch in `%1$*3$.*2$ld`-style positional-argument width/precision handling — it
reproduces consistently on this macOS/arm64 host and is most likely a libc `snprintf`
behavioral difference from the glibc/Linux environment neovim's CI actually runs on,
rather than a bug introduced by the coverage instrumentation (it is unrelated to
`--coverage` and was not investigated further, as it falls outside this analysis's scope
— noted here because it was an incidental, real finding from just running the existing
suite).

**Coverage result** (full report: [`artifacts/coverage/html/index.html`](artifacts/coverage/html/index.html)):

| Scope | Line coverage | Function coverage |
|---|---|---|
| Whole codebase (243 source files) | 6.3% (12,491 / 198,417) | 12.5% (1,106 / 8,837) |
| `src/nvim/base64.c` | **0.0% (0 / 108)** | **0.0% (0 / 2)** |

The low whole-codebase number is expected and not itself a defect: `test/unit` (46 files)
only targets self-contained C modules directly; the bulk of neovim's behavioral coverage
comes from `test/functional` (524 files), which drives a real spawned `nvim` process over
RPC and wasn't instrumented here (running it under `--coverage` is possible but is a much
larger, slower pass; out of scope for this focused analysis). What matters for this report
is the **relative** result: `base64.c` — a small, self-contained function that
encodes/decodes untrusted-shaped input — has *zero* coverage from upstream's own tests.
That is exactly the kind of gap fuzzing and bounded verification are suited to.

## 4. Fuzz testing (`base64_decode`/`base64_encode`, LLVM libFuzzer)

**Target**: [`src/nvim/base64.c`](../neovim/src/nvim/base64.c) — chosen because it is one
of the few files in this codebase that is genuinely self-contained (its only external
dependency is `xmalloc`/`xfree`), has zero existing test coverage (§3), and does exactly
the kind of "decode a length-prefixed, attacker-shaped byte string" work fuzzing is built
for.

**Harness** ([`artifacts/fuzz_base64/harness.c`](artifacts/fuzz_base64/harness.c)):
`LLVMFuzzerTestOneInput` feeds raw fuzzer bytes into `base64_decode()`. If decoding
succeeds, it checks a round-trip property: re-encoding the decoded bytes and decoding
that again must reproduce the original bytes exactly. `xmalloc`/`xfree` are given minimal
standalone definitions rather than linking upstream's real `memory.c`, so the harness
compiles the *actual, unmodified* `base64.c` against nvim's real generated headers
without pulling in the rest of the editor.

**Build**: standard course convention, using Homebrew LLVM for a working fuzzer runtime:
```
clang -I build/src/nvim/auto -I build/include -I build/cmake.config -I src \
  -std=gnu99 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
  harness.c src/nvim/base64.c -o fuzz_base64
```

**Result**: two runs totaling **~219.5 million executions** (119M + 100M, ~90s each) with
ASan and UBSan both active — **zero crashes, leaks, timeouts, or round-trip violations**.
Coverage plateaued at 50/50 edges quickly (most of the function's branch space is small:
padding-length checks, alphabet lookups, invalid-character rejection), and corpus
minimization (`-merge=1`) reduced 38 interesting inputs to 33 without losing coverage.
Full log and minimized corpus: [`artifacts/fuzz_base64/`](artifacts/fuzz_base64/).

**Interpretation**: this is a genuine (if unglamorous) result — for a function with zero
prior test coverage, 220M adversarial-shaped inputs under memory/UB sanitizers found
nothing. It's evidence *for* correctness within the bounds fuzzing can reach, not proof
of it — which is exactly the gap §5 addresses.

## 5. Bounded model checking (`CBMC`)

Fuzzing samples inputs; it can never prove the absence of a bug, only fail to find one in
the time given. CBMC does the complementary thing: for inputs up to a fixed size bound,
it either proves *every* reachable state safe, or produces a concrete counterexample.

**Harness** ([`artifacts/cbmc_base64/harness_cbmc.c`](artifacts/cbmc_base64/harness_cbmc.c)):
a `main()` that declares a 12-byte buffer, marks every byte and the length
nondeterministic (`nondet_char()`, `__CPROVER_assume(src_len <= 12)`), and calls
`base64_decode()` then `base64_encode()` on the result — the same real, unmodified
`base64.c` used for fuzzing.

**Command**:
```
cbmc -I build/src/nvim/auto -I build/include -I build/cmake.config -I src \
  src/nvim/base64.c artifacts/cbmc_base64/harness_cbmc.c --function main \
  --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check \
  --div-by-zero-check --unwind 14 --unwinding-assertions --trace
```

**Result**: `VERIFICATION SUCCESSFUL` — **0 of 466 checked properties failed**
(array-bounds, pointer-dereference-validity, signed/unsigned arithmetic overflow,
divide-by-zero, undefined-shift, and unwinding-sufficiency assertions), for *every*
possible input up to 12 bytes and every possible byte value in it. Full log:
[`artifacts/cbmc_base64/cbmc_output.log`](artifacts/cbmc_base64/cbmc_output.log).

**Interpretation**: combined with §4, this gives two independent, complementary
guarantees for the same previously-untested function: no memory-safety or arithmetic
violation exists for *any* input up to 12 bytes (exhaustive proof), and none was found by
220M adversarial inputs up to 4KB (broad but non-exhaustive sampling). Together they are
considerably stronger evidence than either alone — and neither existed before this
analysis, since `base64.c` had 0% test coverage to begin with.

## 6. Profiling (Valgrind: callgrind, massif, memcheck)

**Workload** ([`artifacts/profiling/workload.lua`](artifacts/profiling/workload.lua)): a
headless Lua script run via `nvim --headless -u NONE -l workload.lua`, exercising four
things in sequence: populating a large buffer, creating tens of thousands of extmarks
(exercising the extmark tree, `src/nvim/marktree.c`), a buffer-wide substitute
(`:%s/quick/QUICK/g` and back), and repeated fuzzy-match queries over a candidate list
(`vim.fn.matchfuzzy`, `src/nvim/fuzzy.c`). Size is controlled by `$NVIM_PROFILE_SCALE` so
each Valgrind tool could use a workload sized for its overhead.

**Native baseline** (macOS, RelWithDebInfo, scale=1.0: 200,000 lines, 50,000 extmarks,
20,000 fuzzy candidates × 20 queries):

| Phase | Time |
|---|---|
| Populate buffer | 0.059s |
| Create extmarks | 0.013s |
| Substitute (×2) | 0.344s |
| Fuzzy match (×20) | 0.079s |
| **Total** | **0.495s** |

The buffer-wide substitute dominates wall time (~70%) even though it's conceptually the
"simplest" operation — worth digging into.

### 6.1 Instruction-level hot path (`callgrind`, scale=0.02, built inside Docker/Ubuntu)

Top instruction consumers ([full annotate](artifacts/profiling/results/callgrind_annotate.txt)),
out of 261.3M total instructions:

| % of instructions | Function |
|---|---|
| 20.55% | `map_key_impl.c.h: mh_find_bucket_int64_t` (×2 inlined copy) |
| 12.90% + 1.29% | `memline.c: ml_find_line` |
| 5.55% | `mh_find_bucket_int64_t` |
| 2.97% + 2.47% | `marktree.c: key_cmp`, `marktree_getp_aux` |
| 1.95% | `mbyte.c: utfc_ptr2len` |
| 0.86% + 0.55% | `regexp.c: vim_regsub_both`, `nfa_regexec_both` |

**This was the non-obvious finding of the whole analysis**: instruction time during a
bulk substitute is dominated by `mh_find_bucket_int64_t` / `ml_find_line` — the hash-map
lookup neovim's `memline.c` uses to map a logical line number to its in-memory block —
not by the regex engine itself (`vim_regsub_both`/`nfa_regexec_both` combined are under
1.5%), and not by the extmark tree (`marktree.c` functions combined are ~5.5%, present
because substituting text also has to keep extmarks in sync, but clearly secondary). The
cost of "substitute across the whole buffer" in this workload is dominated by *finding*
each line, not by matching or rewriting it.

### 6.2 Heap behavior (`massif`, scale=0.2)

Peak heap: **~24.1 MB** at snapshot 71 of 74
([full ms_print output](artifacts/profiling/results/ms_print.txt)). Breakdown of the peak:

- **21.1%** (~5.1 MB) — `undo.c: u_save_line_buf` → `u_savecommon` → `u_savesub`,
  reached via `do_sub` (the `:substitute` command). Every line touched by the substitute
  gets its pre-substitution text `xstrdup`'d into the undo tree *before* the change is
  applied.
- **~52%** (~5.2–12 MB across snapshots) — `memory.c` arena allocator
  (`arena_alloc`/`arena_memdupz`), driven by `nvim_buf_set_lines` and the Lua↔C value
  marshalling (`nlua_pop_Array`/`nlua_pop_Object`) that copies the script's line table
  into the buffer.

**Interpretation**: the heap cost of a bulk substitute isn't the substitute logic itself
— it's the undo system transparently keeping a full backup of every line about to change.
This is expected, correct behavior (that's what undo *is*), but it quantifies a real,
non-obvious cost: editing N lines under `:substitute` allocates roughly N line-copies
purely for undo, on top of whatever the edit itself needs.

### 6.3 Memory-safety spot check (`memcheck --leak-check=full`, scale=0.05)

```
HEAP SUMMARY: in use at exit: 4,505,849 bytes in 61,805 blocks
LEAK SUMMARY:
  definitely lost: 48 bytes in 1 blocks
  indirectly lost:  0 bytes in 0 blocks
  possibly lost:   194 bytes in 8 blocks
  still reachable: 4,505,607 bytes in 61,796 blocks
```

The one "definitely lost" allocation traces entirely through LuaJIT's own JIT
trace-compilation machinery (`lj_mcode_reserve` → `lj_err_register_mcode` →
`__register_frame` in `libgcc_s`) — LuaJIT registering unwind-frame metadata for a
just-in-time-compiled trace. This is a known, benign, one-time artifact of embedding
LuaJIT's JIT (not an interpreter-only build), not a defect in neovim's own C code. Full
log: [`artifacts/profiling/results/memcheck.log`](artifacts/profiling/results/memcheck.log).

## 7. Conclusion

The four analyses reinforce each other rather than standing alone:

1. **Coverage measurement** found a concrete, quantified gap upstream's own extensive
   test suite doesn't fill: `base64.c`, 0% covered.
2. **Fuzzing** filled that gap with breadth — 220M adversarial inputs, zero findings.
3. **CBMC** filled it with depth — an exhaustive proof of memory- and arithmetic-safety
   for all small inputs, a stronger guarantee than sampling can ever give for the sizes it
   covers.
4. **Profiling** answered a different question entirely — not "is this code correct?"
   but "where does its time and memory actually go?" — and the answer (memline's
   line-lookup hash map dominates instructions; the undo tree dominates heap; the regex
   and extmark-tree code most people would guess first are comparatively cheap) was
   genuinely counter-intuitive going in.

None of these four gaps (coverage measurement, fuzzing, formal verification, and a
profiled look at where a common editing operation actually spends its resources) existed
in neovim's own considerable CI investment before this analysis, despite that CI already
running ASan/UBSan/TSan/CodeQL/Coverity on every commit.

## Reproducing this analysis

All harnesses, scripts, and raw tool output are under [`artifacts/`](artifacts/):

- `artifacts/coverage/` — coverage-build `.info` file and HTML report.
- `artifacts/fuzz_base64/` — fuzzer harness, run log, minimized corpus.
- `artifacts/cbmc_base64/` — CBMC harness and full property-check log.
- `artifacts/profiling/` — Dockerfile, workload script, and Valgrind result files.

Tool versions used: CMake 4.4.2, Ninja 1.13.2, `lcov`/`genhtml` 2.5-0, CBMC 6.11.0,
Homebrew LLVM/clang 22.1.8 (fuzzing), Valgrind 3.22.0 (inside `ubuntu:24.04`, Docker
Desktop for Mac).
