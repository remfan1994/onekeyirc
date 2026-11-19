#!/usr/bin/env python3
"""Exercise the real install recipe with private executable fixtures.

No installation outside a temporary DESTDIR, no network, and no IRC sessions.
The source fixture exits successfully; the old installed fixture sleeps while
held open. This tests publication semantics without needing curses headers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def exercise(source, case):
    product = 'onekeyirc' if (source / 'doc/onekeyirc.1').is_file() else 'vipirc'
    sleep = Path(shutil.which('sleep')).resolve()
    replacement = Path(shutil.which('true')).resolve()
    old_bytes, new_bytes = sleep.read_bytes(), replacement.read_bytes()
    assert old_bytes != new_bytes
    with tempfile.TemporaryDirectory(prefix=product + '-install-') as temporary:
        directory = Path(temporary)
        build = directory / 'source'
        build.mkdir()
        template = (source / 'Makefile.in').read_text()
        (build / 'Makefile').write_text(re.sub(r'@[A-Z_]+@', '', template))
        shutil.copyfile(replacement, build / product)
        (build / product).chmod(0o755)
        (build / 'doc').mkdir()
        shutil.copyfile(source / 'doc' / (product + '.1'), build / 'doc' / (product + '.1'))
        destdir = directory / 'destination'
        installed = destdir / 'prefix with spaces/bin' / product
        installed.parent.mkdir(parents=True)
        is_directory = case in ('directory', 'directory-symlink')
        fresh = case == 'new-copy-error'
        other = directory / 'unrelated-directory'
        if is_directory:
            if case == 'directory':
                installed.mkdir()
                other = installed
            else:
                other.mkdir()
                installed.symlink_to(other, target_is_directory=True)
            (other / 'keep.txt').write_text('unrelated data\n')
        elif not fresh:
            installed.write_bytes(old_bytes)
            installed.chmod(0o700)
        before = installed.stat() if installed.exists() else None
        child, pinned = None, None
        try:
            if case == 'running':
                pinned = installed.open('rb')
                child = subprocess.Popen([str(installed), '30'], stdin=subprocess.DEVNULL,
                                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                assert child.poll() is None
            env = dict(os.environ, MAKEFLAGS='', LC_ALL='C')
            if case in ('copy-error', 'new-copy-error', 'chmod-error', 'rename-error'):
                tools = directory / 'tools'
                tools.mkdir()
                command = 'cp' if 'copy' in case else 'chmod' if 'chmod' in case else 'mv'
                actual = shutil.which(command)
                assert actual is not None
                if command == 'cp':
                    failure = 'case "$1" in vipirc|onekeyirc) printf partial > "$2"; exit 71;; esac\n'
                elif command == 'chmod':
                    failure = 'if [ "$1" = 755 ]; then exit 72; fi\n'
                else:
                    failure = 'exit 73\n'
                wrapper = tools / command
                wrapper.write_text('#!/bin/sh\n' + failure + 'exec ' + shlex.quote(actual) + ' "$@"\n')
                wrapper.chmod(0o700)
                env['PATH'] = str(tools) + os.pathsep + env.get('PATH', os.defpath)
            command = [os.environ.get('MAKE', 'make'), 'install',
                       'SRC=', 'HEADERS=', 'OBJ=', 'DOCFILES=',
                       'PREFIX=/prefix with spaces', 'DESTDIR=' + str(destdir)]
            result = subprocess.run(command, cwd=build, env=env, capture_output=True,
                                    text=True, timeout=15)
            checks = {}
            if case == 'running':
                checks['install_succeeded'] = result.returncode == 0
                checks['old_process_alive'] = child.poll() is None
                checks['old_inode_bytes_preserved'] = pinned.read() == old_bytes
                checks['new_binary_published'] = installed.read_bytes() == new_bytes
                checks['new_inode'] = installed.stat().st_ino != before.st_ino
                checks['mode_0755'] = installed.stat().st_mode & 0o7777 == 0o755
                invoked = subprocess.run([str(installed)], capture_output=True, timeout=5)
                checks['new_invocation_succeeded'] = invoked.returncode == 0
            elif is_directory:
                checks['install_refused'] = result.returncode != 0
                checks['directory_unchanged'] = sorted(p.name for p in other.iterdir()) == ['keep.txt']
                checks['sentinel_unchanged'] = (other / 'keep.txt').read_text() == 'unrelated data\n'
                checks['destination_type_preserved'] = installed.is_symlink() if case.endswith('symlink') else installed.is_dir()
            else:
                checks['install_refused'] = result.returncode != 0
                if fresh:
                    checks['no_partial_publication'] = not installed.exists()
                else:
                    checks['old_bytes_preserved'] = installed.read_bytes() == old_bytes
                    checks['old_inode_preserved'] = installed.stat().st_ino == before.st_ino
                    checks['old_mode_preserved'] = installed.stat().st_mode & 0o7777 == 0o700
            checks['no_temporary_files_left'] = sorted(p.name for p in installed.parent.iterdir()) == ([] if fresh else [product])
            return {'case': case, 'passed': all(checks.values()), 'checks': checks,
                    'make_exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
        finally:
            if child is not None:
                if child.poll() is None:
                    child.terminate()
                child.communicate(timeout=5)
            if pinned is not None:
                pinned.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    results = [exercise(source, case) for case in
               ('running', 'copy-error', 'new-copy-error', 'chmod-error',
                'rename-error', 'directory', 'directory-symlink')]
    report = {'scope': 'Actual make install recipe with private executable fixtures; no native curses/IRC runtime claim.',
              'source': str(source), 'makefile_sha256': digest((source / 'Makefile.in').read_bytes()),
              'passed': all(item['passed'] for item in results), 'results': results}
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    for item in results:
        failed = ', '.join(key for key, value in item['checks'].items() if not value)
        print(item['case'] + ': ' + ('PASS' if item['passed'] else 'FAIL: ' + failed), flush=True)
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
