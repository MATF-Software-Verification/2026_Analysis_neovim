#!/usr/bin/env bash
# CPU profiling of a headless nvim editing workload with Valgrind's callgrind
# (the course caps this project at a single Valgrind-family tool; callgrind was
# picked because it produced the analysis's most interesting finding -- see
# ../ProjectAnalysisReport.md section "Profiling (Valgrind: callgrind)").
#
# Valgrind does not support macOS on arm64 at all, so this always runs inside the
# Linux container built from ./Dockerfile. The neovim submodule is copied (not
# bind-mounted in place) into a container-local directory before building, so the
# Linux build never touches the host's own native (macOS) neovim/build/ directory.
set -euo pipefail
cd "$(dirname "$0")/.."

docker build -t nvim-vs-valgrind -f valgrind/Dockerfile .

docker run --rm -v "$(pwd)":/work -w /work nvim-vs-valgrind bash -c '
  set -euo pipefail
  rm -rf /tmp/nvim-linux
  cp -a neovim /tmp/nvim-linux
  rm -rf /tmp/nvim-linux/.git /tmp/nvim-linux/build
  make -C /tmp/nvim-linux CMAKE_BUILD_TYPE=RelWithDebInfo -j"$(nproc)"
  NVIM_PROFILE_SCALE=0.02 valgrind --tool=callgrind \
    --callgrind-out-file=valgrind/results/callgrind.out \
    /tmp/nvim-linux/build/bin/nvim --headless -u NONE -l valgrind/workload.lua
  callgrind_annotate valgrind/results/callgrind.out > valgrind/results/callgrind_annotate.txt
'
