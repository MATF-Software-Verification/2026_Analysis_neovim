#!/usr/bin/env bash
# Bounded model checking of the real, unmodified src/nvim/base64.c (neovim v0.12.5)
# with CBMC. See harness_cbmc.c for the property being checked and ../ProjectAnalysisReport.md
# section "Bounded model checking (CBMC)" for the full writeup.
#
# Requires: cbmc, and the neovim submodule configured at least once
#   (cmake --preset default -B neovim/build inside neovim/) so the generated headers exist.
set -euo pipefail
cd "$(dirname "$0")/.."

cbmc \
  -I neovim/build/src/nvim/auto -I neovim/build/include -I neovim/build/cmake.config -I neovim/src \
  neovim/src/nvim/base64.c cbmc/harness_cbmc.c --function main \
  --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check \
  --div-by-zero-check --unwind 14 --unwinding-assertions --trace \
  | tee cbmc/cbmc_output.log
