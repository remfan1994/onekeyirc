#!/usr/bin/env python3
"""Compare the shared C sources and headers in the two source archives.

Run from either archive, passing the root of the other archive. This is a
read-only byte comparison, not a build, formatter, copier, or runtime test.
The standalone Makefile.in lists the common runtime sources and headers;
using those lists also catches a missing peer file instead of comparing
only the intersection of two directory listings.
"""

import argparse
from pathlib import Path
import re
import sys


def source_files(root):
    """Read literal SRC/HEADERS lists without executing Makefile content."""
    template = (root / "Makefile.in").read_text(encoding="ascii")
    template = re.sub(r"\\\r?\n", " ", template)
    # Collect assignment forms before accepting literal lists. Otherwise an
    # appended or overridden list could silently hide a missing shared file.
    assignments = re.findall(
        r"^[ ]*((?:(?:override|export|private)[ \t]+)*)"
        r"(SRC|HEADERS)[ \t]*([:+?!]*=)[ \t]*(.*)$",
        template, re.MULTILINE)
    names = []
    for variable in ("SRC", "HEADERS"):
        matches = [(modifier, operator, value)
                   for modifier, name, operator, value in assignments
                   if name == variable]
        if any(modifier or operator != "="
               for modifier, operator, value in matches):
            raise ValueError("unsupported " + variable + " assignment; use one "
                             "unmodified literal " + variable + " = list")
        if len(matches) != 1:
            raise ValueError("expected one literal " + variable + " list in "
                             + str(root / "Makefile.in"))
        entries = matches[0][2].split("#", 1)[0].split()
        if not entries:
            raise ValueError(variable + " list is empty")
        for name in entries:
            if not re.fullmatch(r"src/[A-Za-z_][A-Za-z_0-9]*\.[ch]", name):
                raise ValueError("unsupported source-list entry: " + name)
            names.append(name)
    if len(names) != len(set(names)):
        raise ValueError("duplicate path in SRC/HEADERS lists")
    return sorted(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("other", type=Path,
                        help="root of the other extracted source archive")
    args = parser.parse_args()
    here = Path(__file__).resolve().parents[1]
    other = args.other.resolve()
    if here == other:
        parser.error("select the other archive, not this source directory")
    try:
        if not (other / "Makefile.in").is_file():
            raise ValueError("other directory has no Makefile.in: " + str(other))
        here_full = (here / "src/ui.c").is_file()
        other_full = (other / "src/ui.c").is_file()
        if here_full == other_full:
            raise ValueError("provide one vipirc tree and one onekeyirc tree")
        standalone, full = (other, here) if here_full else (here, other)
        names = source_files(standalone)
        problems = []
        for name in names:
            left, right = standalone / name, full / name
            if not left.is_file() or not right.is_file():
                problems.append("missing: " + name)
            elif left.read_bytes() != right.read_bytes():
                problems.append("different: " + name)
        if problems:
            for problem in problems:
                print(problem, file=sys.stderr)
            return 1
        print("shared sources: %d C/header files match byte-for-byte" % len(names))
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        print("shared sources: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
