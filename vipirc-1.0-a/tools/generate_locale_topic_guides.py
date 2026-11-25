#!/usr/bin/env python3
"""Deterministic offline help-topic guides from runtime catalogs.

English topic source text remains in src/idle_commands.c. The translated
strings here are display text only, not commands, config or wire messages.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANGS = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')
SECTIONS = {'AUTHENTICATION': ['Use verified TLS with /autoauth sasl USER file PATH or /autoauth sasl USER gpg PATH.', 'Applied authentication settings take effect on the next disconnected /connect; /save persists them.'], 'BOUNCERS (ZNC / soju)': ['Use /panel, select a server, then choose connection type znc or soju.', 'Use one saved profile per upstream network. Select the intended profile before bare /connect when several use the same hostname.', 'Bouncers restore their joined channels. Local auto-join lists are not replayed and /autojoin edits are refused; use /join and /part to change memberships.', "The client connects only to the bouncer. Configure the bouncer's upstream TLS, network authentication and retention separately."], 'SESSION RECOVERY': ['From a shell, run the application with --sessions. Inspect its current attached/detached status and use that listed job ID, not a process ID.', 'If an old SSH terminal still owns the job, run --detach-session NUMBER, then --resume NUMBER. Detach releases the controller while keeping IRC and state alive.', 'Use --close-session NUMBER only when the whole job should end. It closes IRC and transfers and loses unsaved drafts and in-memory history; saved configs and existing logs remain.'], 'TLS': ['Verified TLS is the default. The requested server hostname or numeric address and its certificate chain must verify; there is no verification bypass or silent plaintext fallback.', 'A port number alone does not disable TLS. Explicit --plain or /connect -plain selects unencrypted IRC and cannot use configured account/server credentials.', '--check-config does not perform a handshake or prove that a CA or credential source will work. --test-mode never contacts a server.', 'TLS protects this connection only. With a bouncer, the upstream connection and retained plaintext history are controlled by the bouncer separately.'], 'LOGGING': ['Persistent logging is optional and off by default. /set persistent_logging on enables it; /save output saves that application preference.', 'Logs are plaintext, not encrypted backups. Each file stops at 16 MiB. An unsafe path, write error or busy file stops logging with a visible error while live chat continues.', '/clear clears memory only. Existing log files survive quit, --close-session and configuration saves. Turning logging off stops new records without deleting old ones.'], 'RECONNECT': ['The shell option --reconnect enables retries for this process; it does not save a setting or initiate a connection.', 'Pending chat messages are discarded on disconnect and are never automatically resent.'], 'TROUBLESHOOTING': ['Resume says busy after SSH loss: run --sessions, verify the job, then --detach-session NUMBER followed by --resume NUMBER. See /help sessions before using destructive --close-session.', 'No channels after connect: inspect the server view and finish registration/authentication. Direct profiles use /autojoin list; bouncers restore memberships and use /join or /part.', 'Authentication fails: check the account, the one selected source, private permissions, TLS endpoint and GPG agent availability. /autoauth status shows configured paths without reading secrets.', 'An applied setting seems ignored: connection changes need a disconnected /connect, while saving and startup autoconnect are separate choices. Interface and terminal-signal launch preferences require a fresh launch.', 'Use --check-config for configuration diagnostics and doc/TROUBLESHOOTING.txt for ordered checks. Offline --test-mode tests the interface, not DNS, TLS, credentials or a real bouncer.']}

def render(lang):
    data = json.loads((ROOT / 'i18n' / (lang + '.json')).read_text(encoding='utf-8'))
    parts = ['vipirc / onekeyirc - help topics (' + lang + ')',
             '========================================',
             '',
             'Commands and option tokens stay in English. Refer to MANUAL.txt and SECURITY.md for exact details.',
             '']
    source = (ROOT / 'src' / 'idle_commands.c').read_text(encoding='utf-8')
    for section, lines in SECTIONS.items():
        parts.extend((section, '-' * len(section)))
        for line in lines:
            if line not in data:
                raise ValueError(f'{lang}: missing runtime key {line}')
            if line not in source:
                raise ValueError(f'outdated source key for {section}: {line}')
            parts.append(data[line])
            parts.append('')
    return ('\n'.join(parts).rstrip() + '\n').encode('utf-8')

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    for lang in LANGS:
        path = ROOT / 'doc' / ('TOPICS-' + lang + '.txt')
        expected = render(lang)
        if args.check:
            if not path.exists() or path.read_bytes() != expected:
                raise SystemExit(f'stale topic guide: {path}')
        else:
            path.write_bytes(expected)
    print(f'localized detailed help guides: {len(LANGS)} languages PASS')

if __name__ == '__main__':
    main()
