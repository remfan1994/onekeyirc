#!/usr/bin/env python3
"""Check literal lookup keys and named-field templates against i18n/en.json.

Source-level guard, not a proof of complete localization. Dynamic keys,
preprocessor branches, raw display text and translation quality require
separate inspection. Named templates are checked wherever they are declared,
including arrays and ternaries, not only inside direct irc_tr() calls.
"""
import ast
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Skip comments and character literals. Merge adjacent C string literals.
TOKEN = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|[A-Za-z_]\w*|[^\s]', re.S)
FIELD = re.compile(r'\{[a-z][a-z0-9_]*\}')
# Literal argument positions, zero based. Dynamic arguments are not claimed
# to have been verified; the named-template scan covers their declarations.
LOOKUPS = {'irc_tr': 0, 'cli_usage_line': 1, 'cli_diagnostic': 1,
           'cli_diagnostic_fields': 1, 'display_text': 1,
           'irc_display_render': 2, 'app_log_translated_event': 2}


def tokens(text):
    out = []
    for m in TOKEN.finditer(text):
        raw = m.group()
        if raw.startswith(('/*', '//')):
            continue
        line = text.count('\n', 0, m.start()) + 1
        if raw.startswith('"'):
            try:
                value = ast.literal_eval(raw)
            except (ValueError, SyntaxError):
                out.append(('other', raw, line))
                continue
            if out and out[-1][0] == 'string':
                _, old, start = out.pop()
                out.append(('string', old + value, start))
            else:
                out.append(('string', value, line))
        else:
            out.append(('other', raw, line))
    return out


def literal_argument(ts, pos, target):
    depth = 0
    index = 0
    arg = []
    for t in ts[pos:]:
        if t[0] == 'other':
            if t[1] in ('(', '[', '{'):
                depth += 1
            elif t[1] in (')', ']', '}'):
                if depth == 0:
                    break
                depth -= 1
            elif t[1] == ',' and depth == 0:
                if index == target:
                    break
                index += 1
                continue
        if index == target:
            arg.append(t)
    if index != target:
        return []
    if len(arg) == 1 and arg[0][0] == 'string':
        return arg
    # Common conditional selection of two literal labels. Do not treat
    # arbitrary strings passed to nested calls as translation keys.
    if len(arg) >= 5 and arg[-4][1] == '?' and arg[-3][0] == 'string' and arg[-2][1] == ':' and arg[-1][0] == 'string':
        return [arg[-3], arg[-1]]
    return []


def main():
    keys = set(json.loads((ROOT / 'i18n/en.json').read_text(encoding='utf-8')))
    found = set()
    missing = []
    for path in sorted((ROOT / 'src').iterdir()):
        if path.suffix not in ('.c', '.h') or path.name in ('l10n.h', 'manual.c'):
            continue
        ts = tokens(path.read_text(encoding='utf-8'))
        candidates = []
        for i, t in enumerate(ts):
            kind, value, line = t
            if kind == 'string' and FIELD.search(value):
                candidates.append(t)
            if kind == 'other' and value in LOOKUPS and i + 1 < len(ts) and ts[i+1][1] == '(':
                candidates.extend(literal_argument(ts, i + 2, LOOKUPS[value]))
        for _, key, line in candidates:
            found.add(key)
            if key not in keys:
                missing.append((path.name, line, key))
    for f, line, key in sorted(set(missing)):
        print(f'{f}:{line}: missing key {key!r}')
    print(f'Runtime catalog literal/template inventory: {len(found)} distinct keys; {len(set(missing))} missing')
    return bool(missing)


if __name__ == '__main__':
    raise SystemExit(main())
