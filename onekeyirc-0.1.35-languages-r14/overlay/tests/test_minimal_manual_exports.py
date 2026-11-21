#!/usr/bin/env python3
"""Exercise offline localized manual output, precedence and literal formatting."""
import argparse
import json
import locale
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_minimal_manuals import LANGS, fixture_text, inspect_fixture


def available_utf8_locale():
    candidates = ["C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8"]
    try:
        result = subprocess.run(["locale", "-a"], capture_output=True,
                                timeout=10, check=False)
        if result.returncode == 0:
            candidates.extend(result.stdout.decode("ascii", errors="ignore").splitlines())
    except (OSError, subprocess.SubprocessError):
        pass
    previous = locale.setlocale(locale.LC_CTYPE)
    try:
        for candidate in dict.fromkeys(candidates):
            try:
                locale.setlocale(locale.LC_CTYPE, candidate)
            except locale.Error:
                continue
            if locale.nl_langinfo(locale.CODESET).lower().replace("-", "") == "utf8":
                return candidate
    finally:
        locale.setlocale(locale.LC_CTYPE, previous)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--program", choices=("onekeyirc", "vipirc"), required=True)
    parser.add_argument("--binary", help="optionally test the real client's --manual path")
    parser.add_argument("--english-reference", help="optional pre-change formatter output")
    args = parser.parse_args()
    manuals, report = inspect_fixture(args.fixture, args.program)
    if args.english_reference:
        assert (Path(args.english_reference).read_bytes() ==
                manuals["en"].encode("utf-8")), "English manual changed"

    primary, fallback = (("ONEKEYIRC_LANG", "VIPIRC_LANG")
                         if args.program == "onekeyirc" else
                         ("VIPIRC_LANG", "ONEKEYIRC_LANG"))
    cases = [
        ({primary: "es", fallback: "id", "LC_ALL": "tr"}, "es"),
        ({primary: "", fallback: "ms", "LC_ALL": "de"}, "ms"),
        ({primary: None, fallback: None, "LC_ALL": "fr",
          "LC_MESSAGES": "ru", "LANG": "it"}, "fr"),
        ({primary: None, fallback: None, "LC_ALL": None,
          "LC_MESSAGES": "de", "LANG": "id"}, "de"),
        ({primary: None, fallback: None, "LC_ALL": None,
          "LC_MESSAGES": None, "LANG": "tr_TR.UTF-8"}, "tr"),
        ({primary: "xx", fallback: "id", "LC_ALL": "fr"}, "en"),
        ({primary: "en", fallback: "id", "LC_ALL": "ar"}, "en"),
        ({primary: "ar", fallback: "id", "LC_ALL": "C",
          "ONEKEYIRC_ARABIC_VISUAL": "1", "VIPIRC_ARABIC_VISUAL": "1"}, "ar"),
    ]
    for index, (environment, expected) in enumerate(cases):
        output = fixture_text(args.fixture, "--localized", args.program,
                              overrides=environment)
        assert output == manuals[expected], ("language precedence", index, expected)
    assert fixture_text(args.fixture, "--english", args.program, "ar") == manuals["en"], \
        "English-only formatter must not depend on the process language"
    assert manuals["en"].isascii(), "English source manual must remain ASCII"

    traced = fixture_text(args.fixture, "--keys", args.program).splitlines()
    percents = fixture_text(args.fixture, "--percent", args.program)
    assert percents.count("@translated %s %n 100%@") == len(traced), \
        "translated percent characters were interpreted as printf directives"

    if args.binary:
        binary = str(Path(args.binary).resolve())
        options = ["-i", "--manual"] if args.program == "vipirc" else ["--manual"]
        for language in ("en", *LANGS):
            environment = os.environ.copy()
            environment.update(ONEKEYIRC_LANG=language, VIPIRC_LANG=language,
                               LC_ALL="C", LC_MESSAGES="C", LANG="C",
                               ONEKEYIRC_ARABIC_VISUAL="1", VIPIRC_ARABIC_VISUAL="1")
            result = subprocess.run([binary, *options], cwd=ROOT, env=environment,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    timeout=20, check=False)
            assert result.returncode == 0 and not result.stderr, \
                ("offline CLI manual failed", language, result.returncode)
            assert result.stdout == manuals[language].encode("utf-8"), \
                ("CLI differs from real manual formatter", language)

        utf8_locale = available_utf8_locale()
        assert utf8_locale, "CLI Arabic export test needs an installed UTF-8 locale"
        arabic_exports = []
        for visual in ("0", "1"):
            environment = os.environ.copy()
            environment.update(ONEKEYIRC_LANG="ar", VIPIRC_LANG="ar",
                               LC_ALL=utf8_locale, LC_MESSAGES=utf8_locale,
                               LANG=utf8_locale, ONEKEYIRC_ARABIC_VISUAL=visual,
                               VIPIRC_ARABIC_VISUAL=visual)
            result = subprocess.run([binary, *options], cwd=ROOT, env=environment,
                                    stdin=subprocess.DEVNULL, capture_output=True,
                                    timeout=20, check=False)
            assert result.returncode == 0 and not result.stderr, \
                ("UTF-8 CLI Arabic manual failed", visual, result.returncode)
            text = result.stdout.decode("utf-8")
            assert any(0x0600 <= ord(c) <= 0x06FF for c in text), \
                "UTF-8 CLI Arabic manual fell back to English"
            assert not any(0xFB50 <= ord(c) <= 0xFDFF or
                           0xFE70 <= ord(c) <= 0xFEFF for c in text), \
                "UTF-8 CLI Arabic manual contains visual presentation forms"
            arabic_exports.append(result.stdout)
        # Decorative heading widths can differ from the C-locale fixture.
        # Both real CLI runs use the same verified UTF-8 locale here.
        assert arabic_exports[0] == arabic_exports[1], \
            "visual Arabic mode changed the logical UTF-8 manual export"
        report["cli_utf8_locale"] = utf8_locale
        report["cli_arabic_visual_mode_byte_identical"] = True

    print("offline manual localization checks passed: " +
          json.dumps(report, sort_keys=True, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
