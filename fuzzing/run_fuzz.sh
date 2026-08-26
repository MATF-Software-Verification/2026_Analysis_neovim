#!/usr/bin/env bash
# libFuzzer fuzzing of base64_decode()/base64_encode() from the real, unmodified
# src/nvim/base64.c (neovim v0.12.5). See harness.c for the property checked
# (round-trip invariant, under ASan+UBSan) and ../ProjectAnalysisReport.md section
# "Fuzz testing" for the full writeup.
#
# Requires: a clang with a working -fsanitize=fuzzer runtime. Apple's bundled
# clang can compile that flag but does not ship the fuzzer runtime archive, so on
# macOS install Homebrew LLVM (`brew install llvm`) and point CLANG at it; on Linux
# a stock clang usually works out of the box.
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

"$CLANG" \
  -I neovim/build/src/nvim/auto -I neovim/build/include -I neovim/build/cmake.config -I neovim/src \
  -std=gnu99 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
  fuzzing/harness.c neovim/src/nvim/base64.c -o fuzzing/fuzz_base64

mkdir -p fuzzing/corpus
: > fuzzing/fuzz_run.log
for run in 1 2; do
  echo "=== run $run (${SECONDS_PER_RUN}s) ===" | tee -a fuzzing/fuzz_run.log
  ./fuzzing/fuzz_base64 -max_total_time="$SECONDS_PER_RUN" fuzzing/corpus 2>&1 | tee -a fuzzing/fuzz_run.log
done

rm -rf fuzzing/corpus_minimized
mkdir -p fuzzing/corpus_minimized
./fuzzing/fuzz_base64 -merge=1 fuzzing/corpus_minimized fuzzing/corpus 2>&1 | tee -a fuzzing/fuzz_run.log
