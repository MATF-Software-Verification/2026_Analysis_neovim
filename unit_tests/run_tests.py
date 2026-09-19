#!/usr/bin/env python3
"""Build and run this project's own unit tests against real, unmodified
neovim v0.12.5 source, then measure their code coverage with gcov/lcov.

Two targets are covered, each with its own test file and its own coverage
report:
  - base64.c: base64_encode()/base64_decode() (test_base64.c)
  - msgpack-rpc: unpacker.c/packer.c + the vendored mpack tokenizer
    (test_msgpack.c) -- see ../ProjectAnalysisReport.md, "msgpack-rpc wire
    format", for why this target was added and what it found.

This is a from-scratch reproduction script: every artifact it needs (object
files, the test binaries, the .info files, the HTML reports) is generated
fresh into unit_tests/build/ and unit_tests/coverage_html*/ on each run.

See RunningTests.md for the plain-language walkthrough and
../ProjectAnalysisReport.md for why each target/approach was chosen.
"""
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
UNIT_TESTS_DIR = Path(__file__).resolve().parent
BUILD_DIR = UNIT_TESTS_DIR / "build"
TEST_RUN_LOG = UNIT_TESTS_DIR / "test_run.log"

NEOVIM_DIR = REPO_ROOT / "neovim"

INCLUDE_DIRS = [
    NEOVIM_DIR / "build" / "src" / "nvim" / "auto",
    NEOVIM_DIR / "build" / "include",
    NEOVIM_DIR / "build" / "cmake.config",
    NEOVIM_DIR / "src",
]


def brew_prefix(pkg):
    try:
        out = subprocess.run(
            ["brew", "--prefix", pkg], check=True, capture_output=True, text=True
        )
        return Path(out.stdout.strip())
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None


MSGPACK_INCLUDES = []
for _pkg, _sub in [("libuv", "include"), ("luajit", "include/luajit-2.1")]:
    _prefix = brew_prefix(_pkg)
    if _prefix is not None:
        MSGPACK_INCLUDES.append(_prefix / _sub)

TARGETS = [
    {
        "name": "base64",
        "test_c": UNIT_TESTS_DIR / "tests" / "test_base64.c",
        "sources": [NEOVIM_DIR / "src" / "nvim" / "base64.c"],
        "extra_includes": [],
        "coverage_patterns": ["*/src/nvim/base64.c"],
        "coverage_info": UNIT_TESTS_DIR / "coverage.info",
        "coverage_html": UNIT_TESTS_DIR / "coverage_html",
    },
    {
        "name": "msgpack",
        "test_c": UNIT_TESTS_DIR / "tests" / "test_msgpack.c",
        "sources": [
            NEOVIM_DIR / "src" / "mpack" / "mpack_core.c",
            NEOVIM_DIR / "src" / "mpack" / "object.c",
            NEOVIM_DIR / "src" / "mpack" / "conv.c",
            NEOVIM_DIR / "src" / "nvim" / "msgpack_rpc" / "unpacker.c",
            NEOVIM_DIR / "src" / "nvim" / "msgpack_rpc" / "packer.c",
        ],
        "extra_includes": MSGPACK_INCLUDES,
        "coverage_patterns": [
            "*/src/mpack/mpack_core.c",
            "*/src/mpack/object.c",
            "*/src/mpack/conv.c",
            "*/src/nvim/msgpack_rpc/unpacker.c",
            "*/src/nvim/msgpack_rpc/packer.c",
        ],
        "coverage_info": UNIT_TESTS_DIR / "coverage_msgpack.info",
        "coverage_html": UNIT_TESTS_DIR / "coverage_html_msgpack",
    },
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
    common_flags = ["-std=gnu99", "-g", "-O0", "--coverage"]

    test_log_parts = []

    for target in TARGETS:
        include_flags = [f"-I{inc}" for inc in INCLUDE_DIRS + target["extra_includes"]]
        flags = [*common_flags, *include_flags]

        test_o = BUILD_DIR / f"test_{target['name']}.o"
        binary = BUILD_DIR / f"test_{target['name']}"

        run([cc, *flags, "-c", str(target["test_c"]), "-o", str(test_o)])

        obj_files = [test_o]
        for src in target["sources"]:
            obj = BUILD_DIR / (src.stem + ".o")
            run([cc, *flags, "-c", str(src), "-o", str(obj)])
            obj_files.append(obj)

        run([cc, "--coverage", *[str(o) for o in obj_files], "-o", str(binary)])

        print(f"\n--- running {target['name']} tests ---")
        result = subprocess.run([str(binary)], cwd=BUILD_DIR, capture_output=True, text=True)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        test_log_parts.append(f"=== {target['name']} ===\n{result.stdout}{result.stderr}")
        if result.returncode != 0:
            TEST_RUN_LOG.write_text("\n".join(test_log_parts))
            sys.exit(f"{target['name']} unit tests failed (exit code {result.returncode})")

    TEST_RUN_LOG.write_text("\n".join(test_log_parts))

    print("\n--- measuring coverage ---")
    raw_info = UNIT_TESTS_DIR / "coverage_raw.info"
    if raw_info.exists():
        raw_info.unlink()
    run(
        [
            "lcov",
            "--capture",
            "--directory",
            str(BUILD_DIR),
            "--output-file",
            str(raw_info),
            "--rc",
            "branch_coverage=1",
            # LLVM's gcov-intermediate-format emulation occasionally reports a
            # hit line with no branch data on it (seen on packer.c, a much
            # larger/branchier file than base64.c) -- a known lcov/LLVM-gcov
            # strictness mismatch, not a real data problem; lcov's own
            # warning suggests this exact flag.
            "--ignore-errors",
            "inconsistent,unsupported",
        ]
    )

    for target in TARGETS:
        info = target["coverage_info"]
        if info.exists():
            info.unlink()
        # Scope the report to this target's own sources -- the raw capture
        # also carries gcov data for whatever the compiler pulled in via
        # headers, and for every other target built in the same pass.
        extract_args = []
        for pattern in target["coverage_patterns"]:
            extract_args.append(pattern)
        run(
            [
                "lcov",
                "--extract",
                str(raw_info),
                *extract_args,
                "--output-file",
                str(info),
                "--rc",
                "branch_coverage=1",
                "--ignore-errors",
                "inconsistent,unsupported",
            ]
        )

        html_dir = target["coverage_html"]
        if html_dir.exists():
            shutil.rmtree(html_dir)
        run(
            [
                "genhtml",
                str(info),
                "--output-directory",
                str(html_dir),
                "--rc",
                "branch_coverage=1",
                "--ignore-errors",
                "inconsistent,unsupported,category",
            ]
        )
        print(f"{target['name']} coverage report: {html_dir / 'index.html'}")

    print("\nDone.")


if __name__ == "__main__":
    main()
