#!/usr/bin/env bash
# Security-focused pattern scanning of the real, unmodified neovim v0.12.5 C
# sources with semgrep's public C/security rulesets -- independent of what
# upstream's own CodeQL already runs on every commit (different engine,
# different rule authors).
#
# semgrep was picked because it is NOT covered anywhere in the course exercises;
# see ../ProjectAnalysisReport.md section "Security scanning (semgrep)".
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p semgrep/report

semgrep scan \
  --config p/c \
  --config p/security-audit \
  --config p/cwe-top-25 \
  --metrics=off \
  --sarif --output semgrep/report/semgrep.sarif \
  neovim/src/nvim

semgrep scan \
  --config p/c \
  --config p/security-audit \
  --config p/cwe-top-25 \
  --metrics=off \
  --text --output semgrep/report/semgrep.txt \
  neovim/src/nvim

echo "Reports: semgrep/report/semgrep.sarif, semgrep/report/semgrep.txt"
