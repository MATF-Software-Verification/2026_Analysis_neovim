#!/usr/bin/env bash
# Bounded model checking of two real, unmodified neovim v0.12.5 targets with CBMC:
#   1. src/nvim/base64.c -- see harness_cbmc.c and ../ProjectAnalysisReport.md
#      section "Bounded model checking (CBMC)".
#   2. the msgpack-rpc decode primitives (src/nvim/msgpack_rpc/unpacker.c +
#      the vendored src/mpack/*.c tokenizer) -- see harness_msgpack_cbmc.c and
#      ../ProjectAnalysisReport.md section "msgpack-rpc wire format". This run
#      is the one that found a real integer-underflow bug in unpack_string();
#      see finding_unpack_string_underflow_repro.c for a standalone ASan
#      reproduction of it.
#
# Requires: cbmc, and the neovim submodule configured at least once
#   (cmake --preset default -B neovim/build inside neovim/) so the generated headers exist.
# The msgpack run additionally needs libuv and luajit headers (both pulled in by
# unpacker.c/packer.c's own includes, same as the fuzzing build).
set -euo pipefail
cd "$(dirname "$0")/.."

echo "=== base64.c ==="
cbmc \
  -I neovim/build/src/nvim/auto -I neovim/build/include -I neovim/build/cmake.config -I neovim/src \
  neovim/src/nvim/base64.c cbmc/harness_cbmc.c --function main \
  --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check \
  --div-by-zero-check --unwind 18 --unwinding-assertions --trace \
  | tee cbmc/cbmc_output.log

echo
echo "=== msgpack-rpc (unpack_integer/unpack_string/unpack_array) ==="
# -I cbmc/stub_headers goes first: it shadows <dirent.h> (see that file for
# why) without needing the real libuv/luajit headers to be modified.
#
# This run is EXPECTED to end in VERIFICATION FAILED -- it's the one that
# found the real unpack_string() underflow documented in
# ../ProjectAnalysisReport.md, so the `if` below deliberately doesn't let
# `set -e` treat that as a script failure.
if cbmc \
  -I cbmc/stub_headers \
  -I neovim/build/src/nvim/auto -I neovim/build/include -I neovim/build/cmake.config -I neovim/src \
  -I "$(brew --prefix libuv)/include" -I "$(brew --prefix luajit)/include/luajit-2.1" \
  neovim/src/mpack/mpack_core.c neovim/src/mpack/object.c neovim/src/mpack/conv.c \
  neovim/src/nvim/msgpack_rpc/unpacker.c neovim/src/nvim/msgpack_rpc/packer.c \
  cbmc/harness_msgpack_cbmc.c --function main \
  --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check \
  --div-by-zero-check --unwind 14 --unwinding-assertions --trace \
  | tee cbmc/cbmc_output_msgpack.log; then
  echo "note: expected VERIFICATION FAILED (see ProjectAnalysisReport.md) -- got SUCCESSFUL instead, worth a second look"
else
  echo
  echo "^ VERIFICATION FAILED above is expected: this is the confirmed unpack_string()"
  echo "  underflow documented in ../ProjectAnalysisReport.md. See"
  echo "  finding_unpack_string_underflow_repro.c for a standalone ASan reproduction."
fi
