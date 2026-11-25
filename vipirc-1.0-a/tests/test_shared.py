#!/usr/bin/env python3
"""Exercise the maintainer comparison against private, deliberately bad trees."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def run(source, peer, expected, diagnostic):
    before = {str(path): path.read_bytes() for root in (source, peer)
              for path in root.rglob("*") if path.is_file()}
    result = subprocess.run([sys.executable, str(source / "scripts/check_shared.py"),
                             str(peer)], capture_output=True, text=True, timeout=10)
    assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
    assert diagnostic in result.stdout + result.stderr, (result.stdout, result.stderr)
    after = {str(path): path.read_bytes() for root in (source, peer)
             for path in root.rglob("*") if path.is_file()}
    assert before == after, "comparison unexpectedly modified a source tree"


def main():
    with tempfile.TemporaryDirectory(prefix="irc-shared-doc-test-") as temporary:
        one, vip = (Path(temporary) / name for name in ("one source", "vip source"))
        template = "SRC = src/main.c \\\n    src/core.c\nHEADERS = src/core.h\n"
        for root in (one, vip):
            (root / "src").mkdir(parents=True)
            (root / "scripts").mkdir()
            shutil.copyfile(ROOT / "scripts/check_shared.py", root / "scripts/check_shared.py")
            (root / "Makefile.in").write_text(template)
            for name in ("main.c", "core.c", "core.h"):
                (root / "src" / name).write_text("/* shared fixture */\n")
        (vip / "src/ui.c").write_text("/* full interface only */\n")
        run(one, vip, 0, "3 C/header files match")
        run(vip, one, 0, "3 C/header files match")
        (vip / "src/core.c").write_text("/* changed comment also counts */\n")
        run(one, vip, 1, "different: src/core.c")
        (vip / "src/core.c").write_text("/* shared fixture */\n")
        (vip / "src/core.h").unlink()
        run(one, vip, 1, "missing: src/core.h")
        (one / "src/core.h").unlink()
        run(one, vip, 1, "missing: src/core.h")
        (vip / "src/core.h").write_text("/* shared fixture */\n")
        run(one, vip, 1, "missing: src/core.h")
        (one / "src/core.h").write_text("/* shared fixture */\n")
        run(one, one, 2, "other archive")
        for extra, variable in (
            ("SRC += src/missing.c\n", "SRC"),
            ("HEADERS += src/missing.h\n", "HEADERS"),
            ("override SRC = src/missing.c\n", "SRC"),
            ("export HEADERS = src/missing.h\n", "HEADERS"),
            ("private SRC = src/missing.c\n", "SRC"),
            ("SRC := src/missing.c\n", "SRC"),
        ):
            (one / "Makefile.in").write_text(template + extra)
            run(one, vip, 2, "unsupported " + variable + " assignment")
        (one / "Makefile.in").write_text("SRC = $(SOURCES)\nHEADERS = src/core.h\n")
        run(one, vip, 2, "unsupported source-list entry")
        (one / "Makefile.in").write_text("SRC = src/main.c\n")
        run(one, vip, 2, "expected one literal HEADERS")
        (one / "Makefile.in").write_text(template)
        (vip / "src/ui.c").unlink()
        run(one, vip, 2, "one vipirc tree and one onekeyirc tree")
    print("shared-source helper: both directions, changed/missing files, invalid inputs and read-only behavior passed")


if __name__ == "__main__":
    main()
