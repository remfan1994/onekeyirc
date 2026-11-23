#!/usr/bin/env python3
"""Check exact full-manual exports and defensive fallback without IRC access.

Run after a successful vipirc build. No terminal, network or credentials needed.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

LANGUAGES = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')


def run(binary, lang, override=None):
    env = os.environ.copy()
    env['VIPIRC_LANG'] = lang
    env.pop('ONEKEYIRC_LANG', None)
    if override is None:
        env.pop('VIPIRC_MANUAL_DIR', None)
    else:
        env['VIPIRC_MANUAL_DIR'] = str(override)
    p = subprocess.run([str(binary), '--manual'], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       check=True, timeout=20)
    return p.stdout


def main():
    root = Path(__file__).resolve().parent.parent
    binary = (root / 'vipirc').resolve()
    if not binary.is_file():
        raise SystemExit('vipirc executable missing; build first')
    english = (root / 'MANUAL.txt').read_bytes()
    assert run(binary, 'en') == english, 'English --manual changed'
    for lang in LANGUAGES:
        expected = (root / 'doc' / ('FULL-MANUAL-' + lang + '.txt')).read_bytes()
        assert run(binary, lang) == expected, lang + ' full manual was not loaded exactly'
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        target = directory / 'FULL-MANUAL-es.txt'
        for data in (b'a\x1b[31m\n', b'a\xe2\x80\xae\n', b'a\x00b\n',
                     b'a\rfoo\n', b'a\xc0\x80\n', b'a'*9000+b'\n',
                     b'a\n'*2050, b'a'*530000):
            target.write_bytes(data)
            assert run(binary, 'es', directory) == english, 'Unsafe file was accepted'
        target.unlink()
        target.symlink_to(root / 'doc' / 'FULL-MANUAL-es.txt')
        if hasattr(os, 'O_NOFOLLOW'):
            assert run(binary, 'es', directory) == english, 'Symlink was accepted'
        target.unlink()
        os.mkfifo(target)
        assert run(binary, 'es', directory) == english, 'FIFO blocked or was accepted'
        target.unlink()
        assert run(binary, 'es', '/nonexistent/vipirc-manual-r26') == english
        assert run(binary, 'es', 'relative/path') == english
    print('full manual exports and unsafe-resource fallback passed')


if __name__ == '__main__':
    main()
