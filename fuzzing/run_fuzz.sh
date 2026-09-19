#!/usr/bin/env bash
# libFuzzer fuzzing of two real, unmodified neovim v0.12.5 targets:
#   1. base64_decode()/base64_encode() (src/nvim/base64.c) -- harness.c
#      (decode-first) and harness_encode.c (encode-first). See each harness
#      file for the exact property checked and ../ProjectAnalysisReport.md
#      section "Fuzz testing" for the full writeup.
#   2. the msgpack-rpc decode/encode primitives (src/nvim/msgpack_rpc/{un,}packer.c
#      + the vendored src/mpack/*.c tokenizer) -- harness_msgpack.c (decode-first)
#      and harness_msgpack_encode.c (encode-first). See
#      ../ProjectAnalysisReport.md section "msgpack-rpc wire format".
#
# Requires: a clang with a working -fsanitize=fuzzer runtime. Apple's bundled
# clang can compile that flag but does not ship the fuzzer runtime archive, so on
# macOS install Homebrew LLVM (`brew install llvm`) and point CLANG at it; on Linux
# a stock clang usually works out of the box. The msgpack harnesses additionally
# need libuv and luajit headers (already required to build the neovim submodule
# itself, so any environment that got this far already has them).
#
# Usage: ./fuzzing/run_fuzz.sh [seconds-per-run]
set -euo pipefail
cd "$(dirname "$0")/.."

SECONDS_PER_RUN="${1:-90}"
CLANG="${CLANG:-}"
if [ -z "$CLANG" ] && command -v brew >/dev/null 2>&1 && brew --prefix llvm >/dev/null 2>&1; then
    CLANG="$(brew --prefix llvm)/bin/clang"
fi
CLANG="${CLANG:-clang}"

EXTRA_INC=()
if command -v brew >/dev/null 2>&1; then
    if brew --prefix libuv >/dev/null 2>&1; then
        EXTRA_INC+=("-I$(brew --prefix libuv)/include")
    fi
    if brew --prefix luajit >/dev/null 2>&1; then
        EXTRA_INC+=("-I$(brew --prefix luajit)/include/luajit-2.1")
    fi
fi

: >fuzzing/fuzz_run.log

run_harness() {
    local label="$1" harness_src="$2" binary="$3" corpus="$4" corpus_min="$5"
    shift 5
    local extra_sources=("$@")

    echo "=== $label harness: build ===" | tee -a fuzzing/fuzz_run.log
    "$CLANG" \
        -I neovim/build/src/nvim/auto -I neovim/build/include -I neovim/build/cmake.config -I neovim/src \
        "${EXTRA_INC[@]}" \
        -std=gnu99 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
        "$harness_src" "${extra_sources[@]}" -o "$binary"

    mkdir -p "$corpus"
    for run in 1 2; do
        echo "=== $label harness: run $run (${SECONDS_PER_RUN}s) ===" | tee -a fuzzing/fuzz_run.log
        "./$binary" -max_total_time="$SECONDS_PER_RUN" "$corpus" 2>&1 | tee -a fuzzing/fuzz_run.log
    done

    rm -rf "$corpus_min"
    mkdir -p "$corpus_min"
    echo "=== $label harness: merge/minimize ===" | tee -a fuzzing/fuzz_run.log
    "./$binary" -merge=1 "$corpus_min" "$corpus" 2>&1 | tee -a fuzzing/fuzz_run.log
}

run_harness "base64 decode-first" fuzzing/harness.c fuzzing/fuzz_base64 \
    fuzzing/corpus fuzzing/corpus_minimized \
    neovim/src/nvim/base64.c
run_harness "base64 encode-first" fuzzing/harness_encode.c fuzzing/fuzz_base64_encode \
    fuzzing/corpus_encode fuzzing/corpus_encode_minimized \
    neovim/src/nvim/base64.c

MSGPACK_SOURCES=(
    neovim/src/mpack/mpack_core.c neovim/src/mpack/object.c neovim/src/mpack/conv.c
    neovim/src/nvim/msgpack_rpc/unpacker.c neovim/src/nvim/msgpack_rpc/packer.c
)
run_harness "msgpack decode-first" fuzzing/harness_msgpack.c fuzzing/fuzz_msgpack \
    fuzzing/corpus_msgpack fuzzing/corpus_msgpack_minimized \
    "${MSGPACK_SOURCES[@]}"
run_harness "msgpack encode-first" fuzzing/harness_msgpack_encode.c fuzzing/fuzz_msgpack_encode \
    fuzzing/corpus_msgpack_encode fuzzing/corpus_msgpack_encode_minimized \
    "${MSGPACK_SOURCES[@]}"
