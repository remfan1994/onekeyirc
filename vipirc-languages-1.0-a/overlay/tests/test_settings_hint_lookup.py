#!/usr/bin/env python3
"""Compile the actual settings field table and hint-selection helpers without curses.

Run after applying the overlay to a clean English r1 tree. This is a targeted
source-extracted unit test, not a full-screen integration test.
"""
import json, os, pathlib, re, shlex, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
s = (root / 'src/settings.c').read_text()
keys = json.loads((root / 'tests/settings-hint-keys.json').read_text())

def section(a, b):
    start = s.index(a)
    return s[start:s.index(b, start)]

# Detect future omissions even if a field is inserted into a table rather than
# written as a direct irc_tr() call.
field_table = section('static const Field fields[]', '#define BASE_FIELDS')
expected = []
for line in field_table.splitlines():
    strings = re.findall(r'"([^"\n]+)"', line)
    if re.match(r'    (?:TXT|SECRET|NUM|BOOL|CHOICE)\("', line):
        expected.append(strings[-1])
    elif re.match(r'    \{"[^"\n]+", "[^"\n]+"', line):
        expected.append(strings[1])
expected.extend(re.findall(r'"([^"\n]+)"', section('static const char *field_hint(', 'static void field_value(')))
assert set(keys) == set(expected), (set(keys)-set(expected), set(expected)-set(keys))

assert 'snprintf(detail, detail_cap, "%s", field_display_hint(s, index));' in s
assert 'const char *hint = field_display_hint(s, field_index(s, s->selected));' in s
assert 'text(LINES - 4, 0, COLS, hint, A_DIM)' in s
assert 'slot->saved ? "" : irc_tr("unsaved")' in (root/'src/ui.c').read_text()
source = '''#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
'''
source += section('typedef enum FieldKind', 'static int irc_label_cells')
source += section('#define TXT(', '/* base/work record')
source += 'typedef struct Settings { Config work; int new_server; } Settings;\n'
source += section('static size_t ignore_fields_start', 'static size_t field_count')
source += section('static int bouncer_identity_field', 'static void set_choice')
source += section('static const char *field_hint(', 'static void field_value(')
source += '''
static const char *keys[] = {''' + ',\n'.join(json.dumps(k) for k in keys) + '''};
static int seen[sizeof keys / sizeof *keys];
static void test_hint(const Settings *s, size_t index, int english)
{
    const char *key = s->new_server && index == 0 ? "Hostname or numeric IP address." :
                      s->new_server && index == 3 ? "Nickname for this server." : field_hint(s, index);
    const char *value = field_display_hint(s, index);
    assert(strcmp(value, irc_tr(key)) == 0);
    for (size_t i=0; i<sizeof keys/sizeof *keys; i++) {
        if (strcmp(key, keys[i]) != 0) continue;
        assert((strcmp(value, key) == 0) == english);
        assert(strlen(value) < 512);
        seen[i] = 1;
        return;
    }
    assert(!"Unexpected settings hint key");
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    Settings s = {0};
    const int types[] = {CONFIG_BOUNCER_NONE, CONFIG_BOUNCER_ZNC, CONFIG_BOUNCER_SOJU};
    int english = !strcmp(argv[1], "en");
    for (size_t t=0; t<sizeof types/sizeof *types; ++t) {
        s.work.bouncer_type = types[t];
        for (int new_server=0; new_server<=1; ++new_server) {
            s.new_server = new_server;
            for (size_t i=0; i<BASE_FIELDS; ++i) test_hint(&s, i, english);
            for (size_t n=0; n<=CONFIG_WORD_MAX; ++n) {
                s.work.word_count = n;
                for (size_t m=0; m<=CONFIG_IGNORE_MAX; ++m) {
                    s.work.ignore_count = m;
                    size_t end = ignore_fields_start(&s) + m + (m < CONFIG_IGNORE_MAX ? 1u : 0u);
                    for (size_t i=BASE_FIELDS; i<end; ++i) test_hint(&s, i, english);
                }
            }
        }
    }
    for (size_t i=0; i<sizeof keys/sizeof *keys; ++i) assert(seen[i]);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='settings-hint-') as td:
    tmp = pathlib.Path(td)
    (tmp/'test.c').write_text(source)
    subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-O1', '-I'+str(root/'src'), str(tmp/'test.c'), '-o', str(tmp/'test')],check=True)
    for lang in 'en id ms tr es pt fr de ru it ar'.split():
        subprocess.run([str(tmp/'test'),lang],env={**os.environ,'VIPIRC_LANG':lang},check=True)
        print(lang, '51 settings hints, base, bouncer and dynamic field selections: PASS')
