#!/usr/bin/env python3
"""Export and audit complete minimalist manuals through the real C formatter.

Build tests/test_manual_localization.c in the application's normal namespace,
then run this tool with --fixture PATH --program onekeyirc|vipirc. Unlike the
fixed help reference, these documents include generated application defaults,
command descriptions, command-system paragraphs, usage and literal examples.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from audit_help_coverage import LANGS, collect, literal_alias

ROOT = Path(__file__).resolve().parents[1]
MARKER = "@TRANSLATED@"
LANGUAGE_ENV = ("ONEKEYIRC_LANG", "VIPIRC_LANG", "LC_ALL", "LC_MESSAGES", "LANG")


def fixture_text(fixture, mode, program, language="en", overrides=None):
    """Run without a terminal and without inheriting the caller's language."""
    environment = os.environ.copy()
    for name in LANGUAGE_ENV:
        environment.pop(name, None)
    environment.update(ONEKEYIRC_LANG=language, VIPIRC_LANG=language,
                       LC_ALL="C", LANG="C")
    if overrides:
        for name, value in overrides.items():
            if value is None:
                environment.pop(name, None)
            else:
                environment[name] = value
    result = subprocess.run([str(Path(fixture).resolve()), mode, program],
                            cwd=ROOT, env=environment,
                            stdin=subprocess.DEVNULL, capture_output=True,
                            timeout=20, check=False)
    if result.returncode or result.stderr:
        detail = result.stderr.decode("ascii", errors="replace").strip()
        raise ValueError(f"manual fixture {mode} failed ({result.returncode}): {detail}")
    return result.stdout.decode("utf-8")


def _string_list(path):
    values = json.loads(path.read_text("utf-8"))
    if (not isinstance(values, list) or
            any(not isinstance(value, str) or not value for value in values) or
            len(values) != len(set(values))):
        raise ValueError(f"{path.name} must contain distinct nonempty strings")
    return set(values)


def inspect_fixture(fixture, program):
    """Audit source-to-runtime coverage and return the eleven export texts."""
    master = _string_list(ROOT / "i18n/en.json")
    generated = _string_list(ROOT / "tools/generated_manual_keys.json")
    paragraphs = _string_list(ROOT / "tools/manual_paragraph_keys.json")
    literals = _string_list(ROOT / "tools/manual_literal_keys.json")
    if not all(value.startswith("/") for value in literals):
        raise ValueError("manual literal manifest contains a non-command example")
    if not (generated | paragraphs) <= master:
        raise ValueError("generated/command-system prose is missing from the master catalog")
    if literals & master:
        raise ValueError("literal command examples must not become translation entries")

    rows = collect(ROOT)
    aliases = {source for kind, name, source in rows
               if literal_alias(kind, name, source)}
    topics = {name for kind, name, _ in rows if kind == "topic"}
    english = fixture_text(fixture, "--english", program)
    traced = fixture_text(fixture, "--keys", program).splitlines()
    keys = set(traced)
    missing = keys - master - literals
    if missing:
        raise ValueError("untranslated runtime prose keys: " +
                         json.dumps(sorted(missing), ensure_ascii=True))
    if not literals <= keys:
        raise ValueError("literal example manifest does not match the real manual")
    if not (paragraphs | aliases) <= keys:
        raise ValueError("manual omits command-system paragraphs or literal aliases")
    prose = keys - literals - aliases

    # Every remaining complete line must be an intentional topic identifier.
    # Literal examples are also looked up by paragraphs(), so the explicit
    # manifest above prevents a new explanatory /command sentence being hidden
    # by a blanket rule that treats all slash-prefixed lines as examples.
    marked = fixture_text(fixture, "--mark", program)
    unmarked = {line for line in marked.splitlines()
                if line and MARKER not in line and set(line) != {"-"}}
    if unmarked != topics:
        raise ValueError("prose bypasses translation or a topic identifier changed: " +
                         json.dumps(sorted(unmarked ^ topics), ensure_ascii=True))
    if len(marked.splitlines()) != len(english.splitlines()):
        raise ValueError("translation changes the number of manual lines")

    english_lines = Counter(english.splitlines())
    usages = [line[len("Usage: "):] for line in english.splitlines()
              if line.startswith("Usage: ")]
    commands = re.compile(r"^/([a-z][a-z_-]*) - ", re.M)
    command_names = commands.findall(english)
    translated_commands = re.compile(r"(?<![\w/])/[a-z][a-z_-]*")
    # The interface switches are case-sensitive: -N must never become -n.
    options = re.compile(r"(?<![\w-])(?:--[a-z][a-z-]*|-[iNn])(?![\w-])")
    protected = ("onekeyirc", "vipirc", "1keyirc/config.ini", "ncurses", "idle",
                 "$XDG_CONFIG_HOME/onekeyirc/config.ini",
                 "$XDG_CONFIG_HOME/vipirc/config.ini",
                 "$HOME/.config/onekeyirc/config.ini",
                 "$HOME/.config/vipirc/config.ini", "XDG_CONFIG_HOME",
                 "[interface]", "[history]", "history_lines=", "lines=",
                 "100", "500", "www.bloodguiltcurse.net")

    manuals = {"en": english}
    for language in LANGS:
        catalog = json.loads((ROOT / "i18n" / f"{language}.json").read_text("utf-8"))
        if set(catalog) != master:
            raise ValueError(f"{language}: catalog/master key mismatch")
        output = fixture_text(fixture, "--localized", program, language)
        output_lines = Counter(output.splitlines())
        if len(output.splitlines()) != len(english.splitlines()):
            raise ValueError(f"{language}: manual line count changed")
        for source in sorted(prose):
            value = catalog[source]
            if not value or value == source:
                raise ValueError(f"{language}: untranslated prose: {source}")
            if value not in output:
                raise ValueError(f"{language}: rendered prose is omitted or truncated: {source}")
        for source in aliases:
            if catalog[source] != source:
                raise ValueError(f"{language}: literal alias mapping changed: {source}")
        for literal in literals | aliases | topics:
            if output_lines[literal] != english_lines[literal]:
                raise ValueError(f"{language}: literal example/identifier changed: {literal}")
        actual_usages = [line[len(catalog["Usage:"]) + 1:]
                         for line in output.splitlines()
                         if line.startswith(catalog["Usage:"] + " ")]
        if actual_usages != usages or commands.findall(output) != command_names:
            raise ValueError(f"{language}: command names or usage syntax changed")
        for source in (generated | paragraphs) & keys:
            value = catalog[source]
            if (Counter(translated_commands.findall(value)) !=
                    Counter(translated_commands.findall(source)) or
                    Counter(options.findall(value)) != Counter(options.findall(source))):
                raise ValueError(f"{language}: generated help command/option changed: {source}")
            for token in protected:
                if token in source and token not in value:
                    raise ValueError(f"{language}: generated help lost {token}: {source}")
        if language == "ar" and any(0xFB50 <= ord(c) <= 0xFDFF or
                                     0xFE70 <= ord(c) <= 0xFEFF for c in output):
            raise ValueError("ar: exported manual must contain logical UTF-8, not presentation forms")
        manuals[language] = output

    return manuals, {"program": program, "lookup_keys": len(keys),
                     "prose_keys": len(prose), "literal_examples": len(literals),
                     "literal_aliases": len(aliases), "topic_identifiers": len(topics),
                     "english_sha256": hashlib.sha256(english.encode("utf-8")).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True,
                        help="compiled tests/test_manual_localization.c fixture")
    parser.add_argument("--program", choices=("onekeyirc", "vipirc"), required=True)
    parser.add_argument("--check", action="store_true", help="fail if any export is stale")
    args = parser.parse_args()
    try:
        manuals, report = inspect_fixture(args.fixture, args.program)
        stale = []
        for language, text in manuals.items():
            path = ROOT / "doc" / f"MINIMAL-MANUAL-{language}.txt"
            data = text.encode("utf-8")
            if path.is_file() and path.read_bytes() == data:
                continue
            if args.check:
                stale.append(path.name)
            else:
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(data)
        if stale:
            raise ValueError("stale complete manuals: " + ", ".join(stale))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(f"{args.program}: 10 translated manuals + English; "
          f"{report['prose_keys']} prose keys; "
          f"{report['literal_examples']} literal examples; "
          f"{report['literal_aliases']} literal aliases; "
          f"{report['topic_identifiers']} topic identifiers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
