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

### 1.1 A second target: the msgpack-rpc wire format

`base64.c` alone left this project's own unit-testing/fuzzing/CBMC work limited to a single
function, even though cppcheck and semgrep already sweep the whole of `src/nvim`. Rather than
stop there, the `neovim/` submodule's own source tree was searched directly for another
candidate in the same shape as `base64.c`: small, self-contained, and doing exactly the "parse
attacker-shaped bytes" work these techniques are built for.

**msgpack-rpc's decode/encode primitives** (`src/nvim/msgpack_rpc/unpacker.c`/`packer.c`) stood
out for a concrete reason, confirmed by trial-compiling each candidate file standalone rather
than guessing from a `grep` for `curbuf`/`emsg`: their low-level functions —
`unpack_integer()`/`unpack_uint_or_sint()`/`unpack_string()`/`unpack_array()`/`unpack_skip()` on
the decode side, `mpack_integer()`/`mpack_uint64()`/`mpack_str()`/`mpack_bin()`/`mpack_raw()` on
the encode side — sit directly on top of the vendored MessagePack tokenizer/parser at
`src/mpack/{mpack_core,object,conv}.c` (578 + 200 + 374 lines), which has **zero** dependencies on
the rest of neovim (confirmed by compiling it completely alone: only libc and itself). That's
more self-contained than `base64.c`, not less, and it parses the exact same "untrusted,
length-prefixed, attacker-shaped bytes" shape — here, msgpack-rpc messages and ShaDa file records
(see §5a's finding for where this specifically matters) instead of base64 text.

**What's deliberately out of scope, and why**: `unpacker.c` and `packer.c` as *whole files* are
not clean drop-ins the way `base64.c` was. `unpacker_parse_header()`/`unpacker_advance()`/
`unpack_keydict()` need the generated API dispatch table (`msgpack_rpc_get_handler_for()`), the
arena allocator, and UI-client globals (`grid_line_buf_*`, `ui_client_get_redraw_handler()`,
etc.); `packer.c`'s `mpack_object()`/`mpack_object_inner()` need Lua refs
(`api_free_luaref()`) for its generic-`Object`-tree-walking path. Stubbing all of that to make
these two files link would be a much bigger surface than this project's established "just stub
`xmalloc`/`xfree`" convention — so this project's harnesses call only the five/five low-level
functions listed above, while still compiling `unpacker.c`/`packer.c` as whole, real, unmodified
files (per this project's own rule) with the unreached code paths satisfied by `abort()`-bodied
stubs (documented in each harness) that make an accidental call to them fail loudly instead of
silently returning nonsense.

This target paid off immediately and far beyond a coverage-percentage argument: **CBMC found a
real, exploitable bug** in `unpack_string()` — see §5a for the full writeup, including a
standalone ASan reproduction independent of CBMC. Unlike `base64.c`, coverage of this target was
not separately pre-measured against upstream's `test/unit` suite the same way `base64.c`'s 0%
figure was (§3) — re-running that whole-codebase coverage pass a second time was judged a worse
use of remaining time than just building the harnesses and seeing what they found, which is
exactly what happened.

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

**msgpack-rpc** (`unit_tests/tests/test_msgpack.c`, see §1.1 for the target/scope rationale):
same no-framework `CHECK()`-macro style, targeting `unpack_integer()`/`unpack_string()`/
`unpack_array()` and `mpack_integer()`/`mpack_str()`:

- `test_known_vectors_decode` — hand-built byte sequences straight from the MessagePack spec's
  type-tag table (fixint/uint8/16/32/64, int8/16/32, fixstr/str8), decoded and checked against
  the expected value — an external oracle independent of this codebase's own encoder, the same
  role RFC 4648 plays for `base64.c`.
- `test_roundtrip_integer_boundaries` — every integer at every `mpack_integer()` encoding-width
  boundary, encoded then decoded, deterministically forcing every width branch. Trial-compiling a
  small dump program to print the real encoded bytes (rather than assuming from the
  `uint8/16/32/64` naming) turned up a genuinely non-obvious implementation detail: the
  uint64-tag (`0xcf`) threshold is `0xfffffff` (2^28-1, ~268M), not `0xffffffff` (2^32-1) as the
  type names suggest — values from `0x10000000` through `0xffffffff` get the full 8-byte encoding
  instead of the 4-byte uint32 one. Still spec-compliant (a decoder must accept any valid width),
  just non-minimal; the boundary list is built around the *real* threshold.
- `test_roundtrip_string_lengths` — string lengths swept across the fixstr(0–31)/str8(32–254)/
  str16(255+) boundaries.
- Four rejection tests: a truncated token, a wrong-type token where an integer was expected, a
  string declaring a length past what the buffer actually has left, and a non-array token passed
  to `unpack_array()`.

**Result**: all 7 test functions pass. Coverage, scoped to the 5 files this target spans
(`src/mpack/{mpack_core,object,conv}.c`, `src/nvim/msgpack_rpc/{unpacker,packer}.c`):

| Metric | Result |
|---|---|
| Lines | 13.1% (166/1263) |
| Functions | 20.7% (18/87) |
| Branches | 8.4% (70/830) |

Full report: `unit_tests/coverage_html_msgpack/index.html`. **Interpretation — much lower than
`base64.c`'s 98.1%, and expected to be**: unlike `base64.c`, where the tested functions are
essentially the whole file, this number covers 5 whole files that include large amounts of code
this project deliberately scoped out (§1.1) — `unpacker_parse_header`/`unpacker_advance`/
`unpack_keydict`, `packer.c`'s Lua-ref/object-tree-walking path, and `object.c`'s tree-walking
parser (`mpack_parse`), which is only reached through `unpack_skip()` — a function the fuzz
harnesses drive (§4) but this unit-test suite deliberately doesn't call, to keep its assertions
scoped to functions with a clear, checkable contract. The low percentage is a direct, honest
consequence of that scoping choice, not a sign the *targeted* functions are weakly tested —
`object.c` shows 0% here for exactly that reason.

## 4. Fuzz testing (LLVM `libFuzzer`)

**Target**: the same `base64_decode()`/`base64_encode()`, chosen because it's genuinely
self-contained, has zero existing coverage (§3), and does exactly the "decode a length-prefixed,
attacker-shaped byte string" work fuzzing is built for.

**Two complementary harnesses** target the same pair of functions from opposite directions:

- **`fuzzing/harness.c`** (decode-first): `LLVMFuzzerTestOneInput` feeds raw fuzzer bytes
  directly into `base64_decode()`. If decoding succeeds, it checks a round-trip property:
  re-encoding the decoded bytes and decoding that again must reproduce the original bytes
  exactly. This is the more adversarial direction — since most raw fuzzer bytes are malformed
  base64, it stresses `base64_decode()`'s rejection paths (bad padding, invalid alphabet
  characters, wrong lengths) directly, which is exactly the untrusted-input parsing surface
  fuzzing is built for.
- **`fuzzing/harness_encode.c`** (encode-first, added as a complementary check): feeds raw
  fuzzer bytes into `base64_encode()` first — which accepts *any* byte sequence unconditionally,
  so there's no rejection step to gate on — then checks an encode→decode fidelity round trip:
  decoding the encoded output must reproduce the original bytes exactly. Where the decode-first
  harness exercises decode's robustness on a broad, mostly-malformed input space, this harness
  gives a direct correctness check of the encode/decode correspondence on a narrower,
  always-well-formed one; the two together cover more of the interface than either alone.

**Build**: `clang -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined`, linking
each harness directly against the real, unmodified `base64.c` (see `fuzzing/run_fuzz.sh`, which
builds and runs both).

**Result**: two runs per harness (`fuzzing/fuzz_run.log`, sections clearly labeled per harness)
— no crashes, leaks, timeouts, or round-trip violations in either. Coverage plateaus quickly at
50/50 edges for the decode-first harness (most of the function's branch space is small:
padding-length checks, alphabet lookups, invalid-character rejection). Corpus minimization
(`-merge=1`) reduces each corpus without losing coverage; minimized corpora:
`fuzzing/corpus_minimized/` (decode-first) and `fuzzing/corpus_encode_minimized/`
(encode-first).

**Interpretation**: for a function pair with zero prior test coverage, tens of millions of
adversarial-shaped inputs under memory/UB sanitizers, from both directions of the
encode/decode interface, found nothing. That's evidence *for* correctness within the bounds
fuzzing can reach — sampling, not proof — which is exactly the gap §5 addresses.

**msgpack-rpc** (see §1.1 for the target/scope rationale): the same decode-first/encode-first
pair, retargeted.

- **`fuzzing/harness_msgpack.c`** (decode-first): feeds raw fuzzer bytes into `unpack_skip()`
  (which drives the full tokenizer/parser in `object.c`), `unpack_array()`, `unpack_integer()`,
  and `unpack_string()`, each on its own `{ptr, size}` view of the same bytes. No round-trip
  property to check here (most random bytes aren't valid msgpack) — this harness is purely a
  crash/UB check on the tokenizer's rejection paths, the msgpack analogue of `harness.c`.
- **`fuzzing/harness_msgpack_encode.c`** (encode-first): takes the first 8 fuzzer bytes as an
  `int64_t`, encodes it with `mpack_integer()`, decodes it back with `unpack_integer()`, and
  checks the round trip; does the same for the remaining bytes as a string via
  `mpack_str()`/`unpack_string()`.

**Build**: identical `clang -fsanitize=fuzzer,address,undefined` invocation as the base64
harnesses, compiling each harness directly against the real, unmodified `unpacker.c`/`packer.c`
plus the vendored `src/mpack/{mpack_core,object,conv}.c` tokenizer (see
`fuzzing/run_fuzz.sh`, which now builds and runs all four harnesses).

**Result** (`fuzzing/fuzz_run.log`, sections labeled per harness): decode-first ran ~10.1M total
executions (5,161,605 + 4,952,831 across the two runs) driving `unpack_skip()`'s full
tokenizer/parser plus the three individual functions on the same bytes; encode-first ran ~117.7M
total executions (58,552,772 + 59,188,346) round-tripping arbitrary integers and strings through
`mpack_integer()`/`unpack_integer()` and `mpack_str()`/`unpack_string()`. **No crashes, leaks, or
round-trip violations in either harness.** Minimized corpora: `fuzzing/corpus_msgpack_minimized/`
(decode-first) and `fuzzing/corpus_msgpack_encode_minimized/` (encode-first).

**Interpretation**: fuzzing alone did not find the `unpack_string()` bug documented in §5a within
the time budget run here — consistent with that bug needing a fairly specific byte pattern (a
declared length that lands in a narrow window relative to the header size actually consumed,
§5a) rather than being reachable from a large fraction of the input space. This is itself a useful
data point for this project's overall theme: fuzzing's breadth and CBMC's exhaustiveness are
genuinely complementary, not redundant — here, the bounded, exhaustive technique found something
the adversarial-sampling one (at this time budget) did not.

## 5. Bounded model checking (CBMC)

Fuzzing samples inputs and can never prove the absence of a bug, only fail to find one in the
time given. CBMC does the complementary thing: for inputs up to a fixed size bound, it either
proves *every* reachable state safe, or produces a concrete counterexample.

**Harness** (`cbmc/harness_cbmc.c`): a `main()` that declares a 12-byte buffer, marks every byte
and the length nondeterministic (`nondet_char()`, `__CPROVER_assume(src_len <= 12)`), and calls
`base64_decode()` then `base64_encode()` on the result — the same real, unmodified `base64.c`.

**Command** (`cbmc/run_cbmc.sh`): `--bounds-check --pointer-check --signed-overflow-check
--unsigned-overflow-check --div-by-zero-check --unwind 18 --unwinding-assertions --trace`.

**Result**: `VERIFICATION SUCCESSFUL` — **0 of 464 checked properties failed**
(array-bounds, pointer-dereference-validity, signed/unsigned arithmetic overflow,
divide-by-zero, undefined-shift, and unwinding-sufficiency assertions), for *every* possible
input up to 12 bytes and every possible byte value in it. Full log: `cbmc/cbmc_output.log`.

**Interpretation**: combined with §3 and §4, this gives three independent, complementary
guarantees for the same previously-untested function: passing behavior on a curated set of known
cases and edge lengths (unit tests), no violation found across tens of millions of adversarial
inputs (fuzzing, broad but non-exhaustive), and no violation possible for *any* input up to 12
bytes (CBMC, exhaustive but bounded). None of the three existed before this analysis, since
`base64.c` had 0% test coverage to begin with.

### 5a. msgpack-rpc, and a real finding

**Harness** (`cbmc/harness_msgpack_cbmc.c`): same shape — a 12-byte nondet buffer and length —
calling `unpack_integer()`, `unpack_string()`, and `unpack_array()` on it. `unpack_skip()`
(object.c's full tree-walking parser) is deliberately excluded from this bounded proof: it has a
much larger unwind requirement (unbounded container nesting depth) that would make this proof
intractable at a useful bound; it's covered by fuzzing instead (§4).

**Command**: identical check flags, `--unwind 14` (the internal loops here — `mpack_rvalue()`'s
byte-accumulation loop for a uint64/int64 payload — max out at 8 iterations, so this bound is
generous, not tight, unlike base64's, which needs to cover the full input length).

**A macOS-specific CBMC limitation, worked around**: CBMC's C front-end cannot parse the Apple
Blocks syntax (`int (^)(const struct dirent *)`) that macOS's real `<dirent.h>` declares for
`scandir_b()` under `#ifdef __BLOCKS__` — pulled in transitively via `<uv.h>`, which
`unpacker.c` includes but (confirmed by `nm -u` on the compiled object file) never actually calls
anything from. `cbmc/stub_headers/dirent.h` shadows the system header with one that
`#undef`s `__BLOCKS__` before deferring to it via `#include_next`, which makes the real header
skip that one declaration. Only needed for CBMC — the clang-based fuzzing/unit-test builds use
the real header directly and never hit this parser limitation.

**Result**: `VERIFICATION FAILED` — **1 of 4377 checked properties failed**, in `unpack_string()`:

```
Violated property:
  file neovim/src/nvim/msgpack_rpc/unpacker.c function unpack_string line 550 thread 0
  arithmetic overflow on unsigned - in size2 - (unsigned long int)tok.length
  !overflow("-", size_t, size2, (unsigned long int)tok.length)
```

Full log and counterexample trace: `cbmc/cbmc_output_msgpack.log`.

**The bug**: `unpack_string()` (`neovim/src/nvim/msgpack_rpc/unpacker.c:534-552`):

```c
String unpack_string(const char **data, size_t *size)
{
  const char *data2 = *data;
  size_t size2 = *size;
  mpack_token_t tok;

  int result = mpack_rtoken(&data2, &size2, &tok);   // consumes the type-tag+length header
  if (result || (tok.type != MPACK_TOKEN_STR && tok.type != MPACK_TOKEN_BIN)) {
    return (String)STRING_INIT;
  }
  if (*size < tok.length) {           // BUG: checks *size (before the header was consumed)
    return (String)STRING_INIT;       //      instead of size2 (what's left after it)
  }
  (*data) = data2 + tok.length;
  (*size) = size2 - tok.length;       // underflows whenever size2 < tok.length <= *size
  return cbuf_as_string((char *)data2, tok.length);
}
```

`mpack_rtoken()` consumes 2/3/5 bytes for a `str8`/`str16`/`str32` type-tag+length header before
`unpack_string()`'s own guard runs. That guard compares the declared payload length
(`tok.length`) against `*size` — the size *before* that header was consumed — instead of
`size2`, the size *after*. Any declared length `L` with `size2 < L <= *size` (trivially satisfied
by `L = *size`, i.e. a tag claiming the payload fills the *entire original buffer, header
included*) slips past the guard, and `size2 - tok.length` wraps around to a number near
`SIZE_MAX`. The function's own doc comment ("data and size are preserved... in cause of
failure") is also violated by this path — it isn't treated as a failure at all.

**Standalone confirmation, independent of CBMC** (`cbmc/finding_unpack_string_underflow_repro.c`):
a 5-byte buffer `{0xd9, 0x05, 'a', 'b', 'c'}` — a `str8` tag declaring length 5 (the whole
buffer), with the 2-byte header leaving only 3 actual payload bytes. Built and run with
`-fsanitize=address`:

```
==ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 1
    #0 ... in main
0x... is located 0 bytes after 5-byte region [...]
    allocated by thread T0 here:
    #0 ... in malloc
```

`unpack_string()` returns `String{ .data = buf+2, .size = 5 }` — claiming 5 bytes where only 3
exist — and leaves `*size = 0xfffffffffffffffe`. Reading the claimed string crashes immediately
under ASan; a caller that instead trusts the corrupted `*size` for a *subsequent* unpack call
would believe there are roughly 2^64 bytes of buffer left and could read arbitrarily far past it.

**Where this is actually reachable**: `unpack_string()`/`unpack_integer()`/`unpack_array()` are
called directly, on file-sourced bytes, throughout `src/nvim/shada.c`'s ShaDa (session state:
command history, registers, marks, etc.) file reader — e.g. `shada.c:3336-3347`, parsing a
`kSDItemHistoryEntry` record straight out of the `.shada` file's msgpack-encoded body. That file's
own doc comment on `unpack_string()` even says it's "safe to use e.g. in shada as we have loaded
a complete shada item into a linear buffer" — this bug shows that assumption doesn't actually hold
for a maliciously-crafted item. Concretely: **a crafted `.shada` file — the kind restored via
`nvim -i <file>`, the default `shada` autoload on startup, or `:rshada`, and exactly the kind of
file that ends up in a shared dotfiles repo or a "restore my session" download — can drive this
exact code path with attacker-chosen bytes.** (This project did not attempt to build a full
weaponized exploit chain from the corrupted `*size` through to a concrete information leak or
crash inside a running `nvim`; the ASan reproduction above demonstrates the underlying memory
corruption directly and precisely, which is the claim this report makes.)

**Cross-checked against the other techniques**: neither cppcheck (§7) nor semgrep (§8) — now
also scanning `src/mpack/` (see those sections) — flagged anything at or near
`unpacker.c:550`. This is consistent with this report's overall theme (different techniques
catch genuinely different classes of issues): a pattern/dataflow-based static analyzer has no way
to know that `*size` is "the wrong variable to compare here" without understanding what
`mpack_rtoken()` consumed — that's a semantic property, not a syntactic pattern, and exactly what
bounded symbolic execution over concrete arithmetic (CBMC) is suited to catching that the other
two tools in this project are not.

**Interpretation**: this is the strongest single result in this project. Fuzzing `unpack_string()`
for the same time budget as `base64.c` (§4) did not find this input by random mutation within the
budget run here — CBMC's exhaustive, bounded search over every possible byte value did, on the
very first target this project pointed it at beyond `base64.c`. That's the concrete case for why
"upstream's CI already runs ASan/UBSan/CodeQL/Coverity on every commit" (§1) is not the same
claim as "this code has been model-checked" — none of those tools reason about this class of
property the way CBMC does.

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
of guessing them, filtered to `src/nvim` and (since §1.1's msgpack-rpc target lives partly
outside it) `src/mpack`, `--enable=warning,style,performance,portability`.

**Result**: 2,982 findings total (`cppcheck/report/cppcheck.xml`; categorized counts in
`cppcheck/report/summary.txt`):

| Severity | Count |
|---|---|
| style | 2,815 |
| error | 135 |
| warning | 27 |
| portability | 5 |

The `style` bucket is dominated by `constVariablePointer`/`constParameterPointer`
(missing-`const` suggestions, 1,549 combined) and `badBitmaskCheck` (604) — real but low-severity
style opinions, not correctness bugs. The `error`-severity bucket is more interesting:
`uninitStructMember` (116), `zerodiv` (6), `uninitvar`/`legacyUninitvar` (6), plus a handful of
`internalAstError`/`syntaxError` (cppcheck's own parser giving up on complex macro-heavy code,
not a code defect). Widening the scope to `src/mpack` added findings there too (mostly
`constParameterPointer`/`constParameterCallback` in `unpacker.c`/`packer.c`, plus one `uninitvar`)
but — checked specifically — **nothing at or near `unpacker.c:550`**, the line §5a's CBMC-found
bug is actually on; see that section for what that says about the two techniques'
complementary blind spots.

**Manual verification of a representative finding**: `file_search.c:1188`,
`uninitStructMember` on `file_id.inode`/`file_id.device_id`. Reading the surrounding code shows
`file_id` is populated by `os_fileid(fname, &file_id)` earlier in the function, gated by the same
`if (!url)` condition that gates both the read at line ~1179 and the write cppcheck flagged —
cppcheck's dataflow analysis doesn't track that the two `!url` branches are the same condition,
so it can't see that `file_id` is always initialized before this use. **Confirmed false
positive**, not a real bug. This is included specifically because a static-analysis section that
only reports raw counts without checking whether the tool's claims hold up isn't actually
verification — the same caution applies to the other 134 `error`-severity findings, which were
not individually re-verified given the scope of this project.

## 8. Security scanning (`semgrep`)

**Why semgrep**: no security-scanning tool is covered anywhere in the course exercises — the
second tool satisfying the independent-discovery requirement, and deliberately independent of
what upstream's own CodeQL already runs on every commit (different engine, different rule
authors, so it can surface things CodeQL's own ruleset doesn't check for).

**Configuration** (`semgrep/run_semgrep.sh`): the public `p/c`, `p/security-audit`, and
`p/cwe-top-25` rulesets over all of `src/nvim` and (same reason as §7) `src/mpack`.

**Result**: 69 findings, unchanged by widening the scope to `src/mpack` (zero findings there —
consistent with §5a's bug being a semantic length-check error, not a
`strcat`/`strcpy`-shaped pattern semgrep's rules look for) — `semgrep/report/semgrep.txt`,
`semgrep/report/semgrep.sarif`, all from two rule categories: `insecure-use-strcat-fn` (18 files)
and `insecure-use-string-copy-fn` — `strcpy`/`strncpy` (10 files) — pattern-based warnings against
any use of these functions, regardless of whether the destination buffer is actually big enough.

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
   findings on `base64.c`.
4. **CBMC** filled it with exhaustive depth on `base64.c` — a proof of memory- and
   arithmetic-safety for all small inputs, a stronger guarantee than sampling can ever give for
   the sizes it covers.
5. **Extending unit tests/fuzzing/CBMC to a second target** (§1.1) — the msgpack-rpc wire
   format — turned this from "verify one function" into "verify a second, independently-chosen
   one too," and paid off immediately: **CBMC found a real integer-underflow/out-of-bounds-read
   bug in `unpack_string()`** (§5a), confirmed with a standalone ASan reproduction, reachable via
   attacker-crafted `.shada` files. Neither cppcheck nor semgrep flagged it, and fuzzing didn't
   find it in the time budget run here — the strongest evidence in this whole project for why
   "different verification techniques catch genuinely different things" isn't just a slogan.
6. **Profiling** answered a different question entirely — not "is this code correct?" but "where
   does its time actually go?" — and the answer (memline's line-lookup hash map dominates
   instructions during a bulk substitute; the regex engine most people would guess first is
   comparatively cheap) was genuinely counter-intuitive going in.
7. **Static analysis and security scanning**, picked specifically because the course didn't teach
   them, now sweep `src/mpack` as well as `src/nvim`, and surfaced real classes of findings (a
   flow-insensitivity false positive worth understanding, and a not-yet-exploitable-but-fragile
   string-concatenation pattern) that neither of the course's own covered tools (Clang Static
   Analyzer) nor upstream's CodeQL had flagged in the specific instances checked here — though,
   as §5a/§7/§8 note, neither caught the one bug this project actually found.

None of these results existed in neovim's own considerable CI investment before this analysis,
despite that CI already running ASan/UBSan/TSan/CodeQL/Coverity on every commit.

## Bonus: Coverity (industrial static analysis)

**Motivation**: not one of the six required techniques — §1 already noted that upstream's own CI
runs Coverity nightly, so re-running the same tool over the same code was deliberately left out of
the six as adding no new information. That reasoning turned out to be slightly incomplete:
upstream's nightly Coverity results are private (they aren't published anywhere this project could
read them), so a local run is genuinely new, independently reproducible data, not a repeat of
something already visible. Institutional access to Coverity (Synopsys/Black Duck `cov-analysis
2026.3.2`, licensed to BlueCat Networks) became available mid-project, and the real motivation for
running it was practical: seeing how setting up a commercial/industrial static analyzer actually
works, in contrast to the five free/open tools above, which is why this is documented as a bonus
rather than folded into the six.

**Setup — what running it actually looked like**: this tool has meaningfully more setup friction
than every other tool in this project, worth documenting in its own right:

- **Licensing**: unlike `cppcheck`/`semgrep`/`cbmc` (freely installable), Coverity requires a
  `license.dat` file dropped into `<cov-analysis-install>/bin/`. The BlueCat organization license
  had in fact expired partway through this exploration and had to be renewed before `cov-analyze`
  would run at all (`[FATAL] License authorization failure: License has expired.`) — a
  reproducibility caveat that doesn't exist for any of the six required tools: a reader without an
  active Coverity license cannot run this section at all.
- **Compiler registration**: Coverity's build-capture tools need to know exactly which compiler
  flags are "required" for a given compiler before they'll translate any file compiled with them.
  The generic `cov-configure --clang` template was not sufficient — `cov-translate` rejected every
  file with `[ERROR] This /usr/bin/cc compiler command specifies the following arguments which
  should be marked as required in your configuration`, naming the exact flags (`-O2 -flto=thin
  -arch arm64 -mmacosx-version-min=... -std=gnu99 -fsigned-char -fstack-protector-strong`) neovim's
  build passes. Re-running `cov-configure` with those flags explicitly attached
  (`cov-configure --comptype clangcc --compiler /usr/bin/cc -- <those flags>`) fixed it — a
  one-time, machine-wide step (it edits the shared Coverity install's `coverity_config.xml`, not
  anything in this repository).
- **Build capture without Xcode**: Coverity's normal capture workflow wraps the actual build
  command (`cov-build <build command>`), intercepting every compiler invocation live. On this
  machine that failed immediately (`[ERROR] Unable to find valid Xcode installation, build capture
  cannot continue`) because only Xcode's Command Line Tools were installed, not the full Xcode.app
  — a real macOS-specific limitation, not a project issue (the same category of environment
  constraint as Valgrind's macOS/arm64 unavailability in §2). The fix was a different Coverity
  capture path entirely: replaying CMake's already-generated `compile_commands.json` (LLVM
  Compilation Database format) directly through Coverity's translator —
  `cov-manage-emit --dir cov-int replay-from-script -if compile_commands.json` — which never
  touches `cov-build` or Xcode at all. This is a genuinely useful alternate capture mechanism worth
  knowing about for exactly this situation (or any CI-like environment where a full IDE toolchain
  isn't installed).
- **A gotcha along the way**: while debugging the Xcode issue, an intermediate `ninja -t clean`
  wiped CMake-generated headers (`*.generated.h`) that `compile_commands.json` still referenced —
  195 of 202 translation units then failed translation with `fatal error: 'memory.h.generated.h'
  file not found`. The fix was a plain `ninja -C build` to regenerate them before recapturing.
  Lesson for reproducing this: don't clean the build directory between generating
  `compile_commands.json` and running the capture — `replay-from-script` needs every generated
  header to actually exist on disk, unlike `cov-build`, which would have regenerated them itself
  as part of driving a live build.

**Result**: `cov-analyze --dir cov-int --all --enable-fnptr --enable-virtual
--disable-parse-warnings` (every checker enabled, not a curated profile):

| Metric | Result |
|---|---|
| Files analyzed | 403 (395 C++, 8 C) |
| Total LoC | 433,234 |
| Functions analyzed | 8,997 |
| Paths analyzed | 3,833,244 |
| Time taken | 00:02:19 |
| Defect occurrences | 899 total, across 30 checker categories |

Top categories: `OVERRUN` (258), `STRING_NULL` (204), `NULL_FIELD` (61), `RESOURCE_LEAK` (57),
`DEADCODE` (47), `TAINTED_SCALAR` (41), `INCONSISTENT_UNION_ACCESS` (37), `INTEGER_OVERFLOW` (34);
full breakdown in `coverity/report/summary.txt`. As with §7/§8, running with `--all` enables
aggressive, high-noise checkers (`DEADCODE`, `CONSTANT_EXPRESSION_RESULT`) alongside high-confidence
ones — the same caution applies here as to cppcheck's and semgrep's un-triaged bulk counts: these
899 occurrences were not individually re-verified given this project's scope.

Reporting is entirely local: `cov-format-errors --dir cov-int --json-output-v7 ...` (or
`--html-output ...` for a browsable report) writes results to disk — nothing is uploaded to a
Coverity Connect server, and none of this requires one.

**`base64.c` finding**: both `src/nvim/base64.c` and `src/nvim/lua/base64.c` (the Lua wrapper
around it) were captured and analyzed. **Zero Coverity defects in either file**, across all 30
checker categories. This is a useful data point directly relevant to §§3–5's shared target:
whole-program dataflow static analysis, with every checker on, found nothing in the exact function
the hand-written unit tests, fuzzing, and CBMC all converge on — not because those three techniques
were redundant, but because static analysis is pattern/dataflow-based and isn't built to establish
the same kind of correctness properties (round-trip behavior, exhaustive bounded safety) those
three target. Consistent with this report's overall theme: different verification techniques catch
genuinely different classes of issues.

**Note**: Coverity was not re-run against §1.1's msgpack-rpc target — it's a bonus, license-gated
tool not counted toward the six required techniques (§1), and re-running it was judged a worse use
of remaining time than the work that actually found something (§5a). Whether Coverity's dataflow
analysis would have caught the `unpack_string()` underflow is an open question this project
doesn't answer.

## Reproducing this analysis

Tool versions used: CMake 4.4.2, Ninja 1.13.2, `lcov`/`genhtml` 2.5-0, CBMC 6.11.0, Homebrew
LLVM/clang 22.1.8 (fuzzing), Valgrind 3.22.0 (inside `ubuntu:24.04`, Docker Desktop for Mac),
cppcheck 2.21.0, semgrep 1.174.0, Coverity Static Analysis 2026.3.2 (bonus, license-gated —
see "Bonus: Coverity" above). The msgpack-rpc target (§1.1) additionally needs libuv and luajit
headers (`brew install libuv luajit` on macOS) — both already required to build the `neovim`
submodule itself, so no environment that can build neovim needs anything extra.

See `README.md` §3 for the exact reproduction command for each tool; every tool's directory
(`unit_tests/`, `fuzzing/`, `cbmc/`, `valgrind/`, `cppcheck/`, `semgrep/`, `coverity/`) contains its
own script and raw output.
