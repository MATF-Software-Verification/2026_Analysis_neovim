#!/usr/bin/env bash
# Static analysis of the real, unmodified neovim v0.12.5 C sources with cppcheck,
# driven by the compile_commands.json CMake already generates (so cppcheck sees
# the project's actual include paths/defines instead of guessing them).
#
# cppcheck was picked specifically because it is NOT one of the static analyzers
# covered in the course exercises (those use the Clang Static Analyzer / scan-build);
# see ../ProjectAnalysisReport.md section "Static analysis (cppcheck)".
set -euo pipefail
cd "$(dirname "$0")/.."

COMPILE_DB=neovim/build/compile_commands.json
if [ ! -f "$COMPILE_DB" ]; then
  echo "missing $COMPILE_DB -- configure the neovim submodule first: (cd neovim && cmake --preset default)" >&2
  exit 1
fi

mkdir -p cppcheck/report

cppcheck \
  --project="$COMPILE_DB" \
  --file-filter="$(pwd)/neovim/src/nvim/*" \
  --enable=warning,style,performance,portability \
  --inline-suppr \
  --suppress=missingIncludeSystem \
  --xml --xml-version=2 \
  -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" \
  2> cppcheck/report/cppcheck.xml

# A full cppcheck-htmlreport (one syntax-highlighted page per flagged file,
# embedding the whole source of ~370 files) runs to ~100MB for this codebase --
# too big to commit for what it adds. Keep the raw XML (the real, complete
# result) plus a compact by-severity/by-check summary instead.
python3 - "$PWD/cppcheck/report/cppcheck.xml" > cppcheck/report/summary.txt <<'PYEOF'
import sys
import xml.etree.ElementTree as ET
from collections import Counter

tree = ET.parse(sys.argv[1])
errors = list(tree.getroot().iter("error"))
by_severity = Counter(e.get("severity") for e in errors)
by_id = Counter(e.get("id") for e in errors)

print(f"{len(errors)} findings total\n")
print("By severity:")
for sev, count in by_severity.most_common():
    print(f"  {count:5d}  {sev}")
print("\nBy check id:")
for check_id, count in by_id.most_common():
    print(f"  {count:5d}  {check_id}")
PYEOF

cat cppcheck/report/summary.txt
echo
echo "Full results: cppcheck/report/cppcheck.xml"
