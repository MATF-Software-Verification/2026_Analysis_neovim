# Running the unit tests

Hand-written unit tests for two targets in the real, unmodified neovim v0.12.5 source:
`base64_encode()`/`base64_decode()` (`tests/test_base64.c`), and the msgpack-rpc decode/encode
primitives (`tests/test_msgpack.c`) -- the same two targets `../fuzzing/` and `../cbmc/` also
cover, so the project applies three independent techniques to each and can compare what each
buys. See `../ProjectAnalysisReport.md` ("Unit tests + code coverage", and §1.1/§5a for why and
what the msgpack target found) for the full rationale and results.

## Why not a test framework

`base64.c` only calls `xmalloc()`/`xfree()` and exposes two pure functions with no object/class
state; the msgpack-rpc functions targeted here are similarly pure (see the report for exactly
which ones and why). Neither has anything for a class-oriented framework like the course's
QtTest to attach to. Both test files are plain C with a small `assert()`-style `CHECK()` macro
and a `main()` that runs every test function and reports a pass/fail count -- the whole
"framework" is about 15 lines, duplicated rather than shared (matching this project's convention
elsewhere of a small, self-contained stub block per harness file).

## Prerequisites

- A C compiler (`cc`) that supports `--coverage` (both `clang` and `gcc` work).
- `lcov`/`genhtml` (`brew install lcov` on macOS, `apt install lcov` on Debian/Ubuntu).
- The `neovim` submodule configured at least once, so the generated headers
  test_base64.c/base64.c/test_msgpack.c/unpacker.c/packer.c depend on exist:
  ```sh
  cd neovim && cmake --preset default && cd ..
  ```
  (A full `cmake --build build` is not required just to run these tests --
  configuring is enough to generate the headers -- but running the fuzzing/
  and cbmc/ scripts, or building the actual `nvim` binary, does need it.)
- For the msgpack target only: libuv and luajit headers (`brew install libuv luajit` on macOS) --
  both already required to build the `neovim` submodule itself.

## Running

From the repository root:

```sh
python3 unit_tests/run_tests.py
```

This will, from scratch, for **each** target (base64, then msgpack):

1. Compile its test file and real source file(s) with `--coverage`, into `unit_tests/build/`.
2. Link and run the resulting binary, printing `all tests passed` (exit code 0) or a list of
   failed assertions (non-zero exit code) -- the whole script stops at the first target that
   fails.
3. Once both binaries have run: a single `lcov --capture` over all of `unit_tests/build/`, then
   one `lcov --extract` per target to scope its report to its own source files (the raw capture
   also picks up whatever headers the compiler pulled in, and every other target's files, none of
   which is the code under test for that report).
4. Render `unit_tests/coverage_html/index.html` (base64) and
   `unit_tests/coverage_html_msgpack/index.html` (msgpack) with `genhtml`.

## What the test files cover

**`test_base64.c`**:
- **Known-vector tests** (`test_known_vectors`): encode/decode against the
  standard RFC 4648 base64 examples (`"Man"` -> `"TWFu"`, etc.) -- an oracle
  independent of the implementation, not just a round trip that could hide a
  symmetric encode/decode bug.
- **Round-trip tests** (`test_roundtrip_all_short_lengths`): every input
  length from 0 to 32 bytes, to exercise the 8-byte and 4-byte bulk-copy
  loops in `base64_encode()` plus all three tail-remainder branches (0, 1, or
  2 leftover bytes).
- **Rejection tests**: a length that isn't a multiple of 4, an invalid
  alphabet character, a misplaced `=`, and a wrong padding count -- the four
  distinct ways `base64_decode()` takes its `invalid:` exit path.

**`test_msgpack.c`**:
- **Known-vector tests** (`test_known_vectors_decode`): hand-built byte sequences straight from
  the MessagePack spec's type-tag table, decoded and checked against the expected value.
- **Round-trip tests**: every integer at every `mpack_integer()` encoding-width boundary
  (`test_roundtrip_integer_boundaries`), and every string length across the
  fixstr/str8/str16 boundaries (`test_roundtrip_string_lengths`).
- **Rejection tests**: a truncated token, a wrong-type token, a string declaring a length past
  what the buffer has left, and a non-array token passed to `unpack_array()`.

## Result (last run against commit `5885a30e1e1225349079e7a1c4a3848aa8e43e42`)

All test functions pass in both binaries. Coverage:

**`base64.c`**:

| Metric | Result |
|---|---|
| Lines | 98.1% (106/108) |
| Functions | 100% (2/2) |
| Branches | 85.2% (46/54) |

For comparison, upstream's own `test/unit` suite gets **0%** line coverage on
this file (see `../ProjectAnalysisReport.md` section 1) -- it never exercises
`base64.c` at all. The uncovered lines/branches are almost entirely the
`goto invalid` arithmetic-edge-case checks (e.g. `acc_len > 4`) that are hard
to hit without also being caught by an earlier check; full report:
`coverage_html/index.html`.

**msgpack-rpc** (`src/mpack/{mpack_core,object,conv}.c` +
`src/nvim/msgpack_rpc/{unpacker,packer}.c`):

| Metric | Result |
|---|---|
| Lines | 13.1% (166/1263) |
| Functions | 20.7% (18/87) |
| Branches | 8.4% (70/830) |

Much lower than `base64.c`'s number, and expected to be: this spans 5 whole files that include
large amounts of code deliberately out of scope for this project (the arena/dispatch/UI-client-
dependent parts of `unpacker.c`, `packer.c`'s Lua-ref path, and `object.c`'s tree-walking parser,
only reached via `unpack_skip()` which the fuzz harnesses drive but this unit-test suite
deliberately doesn't call) -- see `../ProjectAnalysisReport.md` §1.1/§3 for the full scoping
rationale. Full report: `coverage_html_msgpack/index.html`.

This target is also where CBMC (`../cbmc/`) found a real bug -- see
`../ProjectAnalysisReport.md` §5a.
