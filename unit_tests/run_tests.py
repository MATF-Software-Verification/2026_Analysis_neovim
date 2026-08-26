#!/usr/bin/env python3
"""Build and run this project's own unit tests for src/nvim/base64.c against
the real, unmodified neovim v0.12.5 source, then measure their code coverage
with gcov/lcov.

This is a from-scratch reproduction script: every artifact it needs (object
files, the test binary, the .info file, the HTML report) is generated fresh
into unit_tests/build/ and unit_tests/coverage_html/ on each run.

See RunningTests.md for the plain-language walkthrough and
../ProjectAnalysisReport.md section "Unit tests + code coverage" for why this
target/approach was chosen.
"""
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
UNIT_TESTS_DIR = Path(__file__).resolve().parent
BUILD_DIR = UNIT_TESTS_DIR / "build"
COVERAGE_HTML_DIR = UNIT_TESTS_DIR / "coverage_html"
COVERAGE_INFO = UNIT_TESTS_DIR / "coverage.info"
TEST_RUN_LOG = UNIT_TESTS_DIR / "test_run.log"

NEOVIM_DIR = REPO_ROOT / "neovim"
BASE64_C = NEOVIM_DIR / "src" / "nvim" / "base64.c"
TEST_C = UNIT_TESTS_DIR / "tests" / "test_base64.c"

INCLUDE_DIRS = [
    NEOVIM_DIR / "build" / "src" / "nvim" / "auto",
    NEOVIM_DIR / "build" / "include",
    NEOVIM_DIR / "build" / "cmake.config",
    NEOVIM_DIR / "src",
]


def run(cmd, **kwargs):
    print("+ " + " ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True, **kwargs)


def main():
    for inc in INCLUDE_DIRS:
        if not inc.is_dir():
            sys.exit(
                f"missing generated header dir {inc} -- configure+build the neovim "
                "submodule first: (cd neovim && cmake --preset default && cmake --build build)"
            )

    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    BUILD_DIR.mkdir(parents=True)

    cc = "cc"
    include_flags = [f"-I{inc}" for inc in INCLUDE_DIRS]
    common_flags = ["-std=gnu99", "-g", "-O0", "--coverage", *include_flags]

    test_o = BUILD_DIR / "test_base64.o"
    base64_o = BUILD_DIR / "base64.o"
    binary = BUILD_DIR / "test_base64"

    run([cc, *common_flags, "-c", str(TEST_C), "-o", str(test_o)])
    run([cc, *common_flags, "-c", str(BASE64_C), "-o", str(base64_o)])
    run([cc, "--coverage", str(test_o), str(base64_o), "-o", str(binary)])

    print("\n--- running tests ---")
    result = subprocess.run([str(binary)], cwd=BUILD_DIR, capture_output=True, text=True)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    TEST_RUN_LOG.write_text(result.stdout + result.stderr)
    if result.returncode != 0:
        sys.exit(f"unit tests failed (exit code {result.returncode})")

    print("\n--- measuring coverage ---")
    if COVERAGE_INFO.exists():
        COVERAGE_INFO.unlink()
    run([
        "lcov", "--capture",
        "--directory", str(BUILD_DIR),
        "--output-file", str(COVERAGE_INFO),
        "--rc", "branch_coverage=1",
    ])
    # Scope the report to base64.c itself -- the object files also carry gcov
    # data for whatever the compiler pulled in via headers, which isn't the
    # code under test.
    run([
        "lcov", "--extract", str(COVERAGE_INFO), "*/src/nvim/base64.c",
        "--output-file", str(COVERAGE_INFO),
        "--rc", "branch_coverage=1",
    ])

    if COVERAGE_HTML_DIR.exists():
        shutil.rmtree(COVERAGE_HTML_DIR)
    run([
        "genhtml", str(COVERAGE_INFO),
        "--output-directory", str(COVERAGE_HTML_DIR),
        "--rc", "branch_coverage=1",
    ])

    print(f"\nDone. Coverage report: {COVERAGE_HTML_DIR / 'index.html'}")


if __name__ == "__main__":
    main()
