#!/usr/bin/env python3
"""Seeded real-PTY editor/modal stress; offline or private loopback fixtures.

The editor model uses ordinary cluster strings and known display widths;
there is no access to application internals. --steps controls reproducible
editing work. --rounds controls modal/resize churn. JSON reports record the
seed, actual child RSS samples where /proc exists, elapsed time and outcomes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import signal
import struct
import sys
import termios
import time
import fcntl

from test_offline import BINARY, OfflineTerminal
from test_input_limits import (BEGIN_PASTE, END_PASTE, WARNING, editor_info, wait_bytes,
                               focused_label, status_focus)
from test_servers import (MultiServerSession, open_servers, in_servers, focused_action,
                          CHANNEL_ACTIONS, QUERY_ACTIONS)
from test_settings import (in_settings, open_settings, select_field, choose, edit_field,
                           HISTORY_FIELD, action as settings_action)
from test_transfers import (IncomingPeer, offer_payload, open_transfers,
                            in_transfers, action as transfer_action)
from process_stats import sample_child

UP, DOWN, LEFT, RIGHT = b"\x1bOA", b"\x1bOB", b"\x1bOD", b"\x1bOC"
HOME, END, DELETE = b"\x1bOH", b"\x1bOF", b"\x1b[3~"
ASCII = list("abCD012 -_/!?[]")
UNICODE = ["\u00e9", "\u4e2d", "\U0001f642", "e\u0301"]
GEOMETRIES = [(9, 20), (10, 21), (12, 40), (19, 59), (24, 80), (32, 132), (42, 180)]


def rss_kib(pid):
    if not sys.platform.startswith('linux'):
        return None
    return sample_child(pid, BINARY, include_children=False)['rss_kib']


def resize(s, rows, cols):
    s.quiet(.02)
    fcntl.ioctl(s.master, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    s.screen.resize(rows, cols)
    s.proc.send_signal(signal.SIGWINCH)
    s.quiet(.035)
    assert s.proc.poll() is None, 'client exited during resize'


def event(x, y, release=False, button=0):
    return ('\x1b[<%d;%d;%d%s' % (button, x + 1, y + 1,
                                 'm' if release else 'M')).encode('ascii')


def click(x, y):
    return event(x, y) + event(x, y, True)


def transfer_action_x(s, label, row):
    s.wait(lambda: label in ''.join(s.screen.cells[row]),
           'near-heading transfer actions did not finish rendering: ' + label)
    line = ''.join(s.screen.cells[row])
    assert label in line, (label, s.screen.text())
    return line.index(label) + min(2, len(label) - 1)


def main_ready(s):
    s.wait(lambda: s.main_frame_ready() and editor_info(s) is not None,
           'main editor did not return')


def no_offline_send(s):
    assert b'<offline_tester>' not in s.raw, 'unexpected ordinary chat submission'
    assert b'* offline_tester ' not in s.raw, 'unexpected action submission'


def return_chat(s):
    if s.panel_title().startswith('vipirc |'):
        s.leave_app()
        return
    if in_transfers(s):
        for unused in range(3):
            prior = s.panel_title(), 'Edit: ' in s.screen.text()
            s.type(b'\x1b')
            s.wait(lambda: (s.panel_title(), 'Edit: ' in s.screen.text()) != prior,
                   'Escape did not leave the current transfer editor/browser/form')
            if not in_transfers(s):
                break
        main_ready(s)
        return
    settings = any(name in s.panel_title() for name in ('Settings','New server','About','Insha-Allah')) or \
               ('Help / Manual' in s.panel_title() and 'Return to: vipirc' in s.screen.text())
    s.type(b'\x1b')
    if settings:
        s.wait(lambda:s.panel_title().startswith('vipirc |'),'Settings did not return to vipirc')
        s.leave_app()
    main_ready(s)
    assert s.mode() == 'INPUT', s.screen.text()


def mouse_transfer_boundaries(offline):
    """Old list coordinates must not Accept a just-created receive review."""
    fixture = MultiServerSession(offline=offline, rows=26, cols=132)
    peer = None
    started = time.monotonic()
    with fixture as s:
        s.start()
        if not offline:
            peer = IncomingPeer()
            offer_payload(s.server, 'MousePeer', 'mouse-proof.bin', peer.port, 1)
            s.wait(lambda: 'File offer from MousePeer' in s.screen.text(), 'incoming offer was not recorded')
        open_transfers(s)
        s.wait(lambda: ('preview.txt' if offline else 'mouse-proof.bin') in s.screen.text(),
               'offer missing from transfer list')
        y = 3
        accept, reject = transfer_action_x(s, 'Accept', y), transfer_action_x(s, 'Reject', y)
        # The old transfer row is also where the new form's actions appear.
        s.type(click(accept, y) + click(reject, y) + click(reject, 4))
        s.wait(lambda: 'Receive file' in s.screen.text(), 'review form did not open')
        s.quiet(.12)
        assert 'disabled' not in ''.join(s.screen.cells[-1]).lower(), (
            'old list Reject coordinates dispatched the new review Accept', s.screen.text())
        assert 'Receiving file' not in s.screen.text(), 'queued mouse event accepted a fresh review'
        assert not s.data_home.exists(), 'queued old-screen event created a destination'
        if peer: peer.assert_unopened()

        # Reverse transition: Cancel returns to the list. Its old Accept
        # coordinate must not act on the newly visible list's Reject button.
        y = 4
        cancel, accept = transfer_action_x(s, 'Cancel', y), transfer_action_x(s, 'Accept', y)
        s.type(click(cancel, y) + click(accept, y) + click(accept, 3))
        s.wait(lambda: 'Receive file' not in s.screen.text() and in_transfers(s),
               'form Cancel did not return to list')
        s.quiet(.08)
        assert 'offered' in s.screen.text().lower(), 'old form coordinate rejected the pending offer'
        if peer: peer.assert_unopened()

        # A row click opens a field editor. Queued coordinates for a different
        # field must not alter the new editor's selection/caret before typing.
        transfer_action(s, 'Send...')
        s.wait(lambda: 'Send file' in s.screen.text(), 'send form missing')
        s.type(b'\r')
        s.wait(lambda: 'Edit: Nickname' in s.screen.text(), 'nickname editor missing')
        s.type(b'ExistingNick\r')
        s.wait(lambda: 'Edit: Nickname' not in s.screen.text(), 'nickname was not staged')
        s.type(click(4, 5) + click(4, 6) + b'MouseNick')
        s.wait(lambda: 'Edit: Nickname' in s.screen.text() and 'MouseNick' in s.screen.text(),
               'send-field transition lost following keyboard data')
        s.wait(lambda: '9 UTF-8 bytes' in s.screen.text(),
               'old form coordinates cancelled new field selection')
        return_chat(s)
        assert not s.data_home.exists(), 'review/editing opened a file'
        if peer:
            peer.assert_unopened()
            assert not any(b'\x01DCC SEND ' in line for unused, line in s.server.lines)
            peer.close()
            peer = None
        assert not any(line.startswith(b'PRIVMSG ') for p in s.peers for unused, line in p.lines)
    return {'name': 'mouse-transfer-' + ('offline' if offline else 'loopback'),
            'seconds': round(time.monotonic() - started, 3),
            'checks': 3, 'terminal_restored': True, 'unintended_sends': 0}


def mouse_view_settings_boundaries():
    started = time.monotonic()
    with OfflineTerminal(rows=26, cols=132) as s:
        s.start();s.input_mode();s.type('CHANNEL-DRAFT');wait_bytes(s,13)
        s.navigate('m');s.input_mode();s.type('MENTIONS-DRAFT');wait_bytes(s,14)
        s.navigate('c');s.wait(lambda:re.match(r'^(?:\[OFFLINE\] )?Channels(?:\s*\||$)',s.panel_title()),'channel list missing')
        s.type(HOME + DOWN + b'\r');main_ready(s);wait_bytes(s,13)
        s.wait(lambda:'Mentions' in ''.join(s.screen.cells[0]),
               'simulated mention activity did not make its category available')
        status=''.join(s.screen.cells[0]);x=status.index('Mentions')+3
        clear_row=s.screen.rows-1
        s.type(click(x,0)+click(2,clear_row))
        s.wait(lambda:'Mentions and unread' in s.panel_title() and s.header_focus() is None and not s.is_main(),
               'combined activity did not finish rendering')
        assert s.header_focus() is None and not s.is_main()
        s.type(b"\x1b")
        main_ready(s);wait_bytes(s,13)
        assert 'CHANNEL-DRAFT' in s.input_text(),'old queued Clear click changed the originating draft'
        s.command('/mentions');main_ready(s);wait_bytes(s,14)
        assert 'MENTIONS-DRAFT' in s.input_text(),'old conversation click cleared another draft'

        # A choice popup closes after its option is clicked. A second click
        # queued against that popup must not activate an underlying list row.
        open_settings(s);select_field(s,8);s.type(b'\r')
        s.wait(lambda:'(*)' in s.screen.text(),'authentication choice missing')
        choice_row=next(row for row,cells in enumerate(s.screen.cells)
                        if re.search(r'\( \) none\b',''.join(cells)))
        choice_line=''.join(s.screen.cells[choice_row])
        choice_col=choice_line.index('none')+1
        s.type(click(choice_col,choice_row)+click(choice_col,choice_row-1))
        s.wait(lambda:'Choice staged' in s.screen.text(),'choice did not stage')
        s.quiet(.06)
        assert '(*)' not in s.screen.text() and 'Edit:' not in s.screen.text(), (
            'old popup click activated the newly revealed settings list',s.screen.text())
        return_chat(s);wait_bytes(s,14)
        open_settings(s);select_field(s,8);s.type(b'\r')
        s.wait(lambda:re.search(r'\(\*\) sasl\b',s.screen.text()), 'cancelled authentication choice became active')
        return_chat(s);wait_bytes(s,14)
        no_offline_send(s)
    return {'name':'mouse-view-settings-boundaries','checks':2,
            'seconds':round(time.monotonic()-started,3),'terminal_restored':True,'unintended_sends':0}


def mouse_transfer_floods():
    """Overflow ordinary click batches without connecting to an offered peer."""
    started=time.monotonic()
    for repeat in (8,32,128):
        peer=IncomingPeer()
        try:
            with MultiServerSession(rows=26,cols=132) as s:
                s.start()
                offer_payload(s.server,'MousePeer','mouse-flood.bin',peer.port,1)
                s.wait(lambda:'File offer from MousePeer' in s.screen.text(),'offer missing')
                open_transfers(s)
                y=3
                accept,reject=transfer_action_x(s,'Accept',y),transfer_action_x(s,'Reject',y)
                # A sufficiently large ncurses mouse queue may discard old
                # coordinates, so the selected final row/form is unspecified.
                # The security invariant is zero peer/file access before the
                # user sees and deliberately acts on a receive review.
                s.type(click(accept,y)+(click(reject,y)+click(reject,4))*repeat)
                s.quiet(.2)
                peer.assert_unopened()
                assert not s.data_home.exists(),'queued click flood created a destination'
                assert not any(line.startswith(b'PRIVMSG ') for unused,line in s.server.lines)
        finally:peer.close()
    return {'name':'mouse-transfer-floods-loopback','click_counts':[17,65,257],
            'seconds':round(time.monotonic()-started,3),'peer_connections':0,
            'filesystem_creations':0,'terminal_restored':True,'unintended_sends':0}


def wide_caret_redraws():
    """A caret highlight must never split a double-width terminal glyph."""
    started=time.monotonic()
    checks=0

    def check_cells(s,parts,row,left,cursor):
        nonlocal checks
        s.quiet(.025)
        col=left
        for part in parts:
            assert s.screen.cells[row][col]==part,('wide glyph redraw changed text',part,col,s.screen.text())
            width=2 if part in ('\u4e2d','\U0001f642') else 1
            if width==2:assert s.screen.cells[row][col+1]=='','second cell of wide glyph was overwritten'
            col+=width
        assert (s.screen.row,s.screen.col)==(row,left+cursor),('hardware caret position changed',s.screen.text())
        assert s.screen.reverse_cells[row][left+cursor],'explicit caret highlight is missing'
        checks+=1

    with MultiServerSession(offline=True,rows=26,cols=132) as s:
        s.start();s.input_mode()
        for parts in (["\u4e2d","A","B","C"],["\U0001f642","0"," ","x","y","z"],
                      ["A","B","C","\u4e2d"],["a","b","\u4e2d","c","d"]):
            s.clear_input();s.type(''.join(parts));info=wait_bytes(s,len(''.join(parts).encode('utf-8')))
            width=sum(2 if part in ('\u4e2d','\U0001f642') else 1 for part in parts)
            for unused in range(3):
                s.type(END);check_cells(s,parts,info.label_row+1,1,width)
                s.type(HOME);check_cells(s,parts,info.label_row+1,1,0)
                s.type(END);check_cells(s,parts,info.label_row+1,1,width)
        s.clear_input()
        for form in ('settings','transfers'):
            if form=='settings':
                open_settings(s);select_field(s,5);s.type(b'\r')
                label='Edit: Real name'
            else:
                s.navigate('t');s.wait(lambda:in_transfers(s),'transfer list missing')
                transfer_action(s,'Send...')
                s.wait(lambda:'Send file' in s.screen.text(),'send form missing')
                s.type(DOWN+b'\r')
                s.wait(lambda:'Choose file' in s.panel_title(),'source-file browser missing')
                transfer_action(s,'Type path');label='Edit: Source file'
            s.wait(lambda:label in s.screen.text(),'wide field editor missing')
            parts=['\u4e2d','A','B','C']
            s.type(''.join(parts)+'\r')
            s.wait(lambda:label not in s.screen.text(),'wide field was not staged')
            s.type(b'\r')
            if form=='transfers':
                s.wait(lambda:'Choose file' in s.panel_title(),'source-file browser did not reopen')
                transfer_action(s,'Type path')
            s.wait(lambda:label in s.screen.text(),'wide field did not reopen')
            check_cells(s,parts,6,2,5)
            for key,cursor in ((HOME,0),(END,5),(HOME,0),(RIGHT,2),(LEFT,0)):
                s.type(key);check_cells(s,parts,6,2,cursor)
            return_chat(s)
        assert not s.data_home.exists()
        assert b'<offline_first>' not in s.raw,'wide redraw test submitted chat'
    return {'name':'wide-caret-redraws','checks':checks,'surfaces':['chat','settings','transfer-field'],
            'seconds':round(time.monotonic()-started,3),'terminal_restored':True,'unintended_sends':0}


class Model:
    def __init__(self):
        self.parts = []
        self.pos = 0
        self.selected = False

    def text(self):
        return ''.join(self.parts)

    def insert(self, parts):
        if self.selected:
            self.parts, self.pos, self.selected = [], 0, False
        self.parts[self.pos:self.pos] = parts
        self.pos += len(parts)

    def clear(self):
        self.parts, self.pos, self.selected = [], 0, False

    def position(self, width):
        row = col = 0
        positions = []
        for part in self.parts:
            size = 2 if part in ('\u4e2d', '\U0001f642') else 1
            if col + size > width:
                row, col = row + 1, 0
            positions.append((row, col))
            col += size
            if col == width:
                row, col = row + 1, 0
        positions.append((row, col))
        return positions[self.pos]


def check_model(s, m, label):
    expected_bytes = len(m.text().encode('utf-8'))
    row, col = m.position(s.screen.cols - 2)
    def correct():
        info = editor_info(s)
        return (info is not None and info.byte_count == expected_bytes and
                (info.row is None or info.row == row + 1) and
                (info.col is None or info.col == col + 1) and
                info.label_row < s.screen.row < s.screen.rows and
                s.screen.col == col + 1 and s.screen.reverse_cells[s.screen.row][col + 1])
    s.wait(correct, label)
    assert s.mode() == 'INPUT', (label, s.screen.text())
    assert WARNING not in s.screen.text(), 'sub-budget model triggered warning'
    no_offline_send(s)


def verify_full_draft(s, m):
    """A large screen exposes this bounded model's entire draft for comparison."""
    original = (s.screen.rows, s.screen.cols)
    resize(s, 30, 180)
    check_model(s, m, 'resizing corrupted model state')
    info = editor_info(s)
    actual = ''.join(''.join(row[1:-1]).rstrip() for row in s.screen.cells[info.label_row + 1:-2])
    # Model text may contain intentional spaces at row ends. Compare each
    # cluster at its independently calculated screen location instead.
    row = col = 0
    for part in m.parts:
        width = 2 if part in ('\u4e2d', '\U0001f642') else 1
        if col + width > 178:
            row, col = row + 1, 0
        cell = s.screen.cells[info.label_row + 1 + row][col + 1]
        assert cell == part, ('draft content changed', row, col, repr(cell), repr(part), repr(actual))
        col += width
        if col == 178:
            row, col = row + 1, 0
    resize(s, *original)
    check_model(s, m, 'return resize corrupted draft/caret')


def editor_fuzz(seed, steps, locale, term, keymap):
    rng = random.Random(seed)
    alphabet = ASCII + (UNICODE if locale != 'C' else [])
    started = time.monotonic()
    samples, trail = [], []
    operations = 0
    with OfflineTerminal(rows=24, cols=100, locale=locale, term=term,
                         keymap=keymap, focus_style='brackets' if keymap == 'vim' else 'reverse') as s:
        s.start(); s.input_mode()
        home = s.home_key
        end = b'\x1b[4~' if term == 'screen' else END
        m = Model()
        for step in range(steps):
            op = rng.choice(['insert'] * 5 + ['left', 'right', 'home', 'end',
                            'backspace', 'delete', 'paste', 'focus', 'clear', 'invalid'])
            if op in ('insert', 'paste'):
                parts = [rng.choice(alphabet) for unused in range(rng.randint(1, 7))]
                if len((m.text() + ''.join(parts)).encode('utf-8')) > 260:
                    op = 'clear'
                else:
                    encoded = ''.join(parts).encode('utf-8')
                    if op == 'insert' and m.pos == 0 and parts[0] == '/':
                        # A deliberately typed leading slash opens Command.
                        # Cancel must restore the model draft/caret exactly;
                        # pasting the same string is then literal chat data.
                        s.type(encoded)
                        s.wait(lambda:s.in_command(), 'leading slash did not open Command during editor churn')
                        s.type(b'\x1b')
                        s.wait(lambda:s.is_main() and s.mode()=='INPUT', 'Command cancel lost the main screen')
                        s.input_mode()
                        check_model(s,m,'Command cancel corrupted model draft/caret')
                        op = 'paste'
                    if op == 'paste': encoded = BEGIN_PASTE + encoded + END_PASTE
                    s.type(encoded); m.insert(parts)
            if op == 'left':
                s.type(LEFT);m.pos = 0 if m.selected else max(0, m.pos - 1);m.selected = False
            elif op == 'right':
                s.type(RIGHT);m.pos = len(m.parts) if m.selected else min(len(m.parts), m.pos + 1);m.selected = False
            elif op == 'home':
                s.type(home);m.pos = 0;m.selected = False
            elif op == 'end':
                s.type(end);m.pos = len(m.parts);m.selected = False
            elif op in ('backspace', 'delete'):
                s.type(b'\x7f' if op == 'backspace' else DELETE)
                if m.selected: m.clear()
                elif op == 'backspace' and m.pos:
                    del m.parts[m.pos - 1];m.pos -= 1
                elif op == 'delete' and m.pos < len(m.parts): del m.parts[m.pos]
            elif op == 'focus':
                s.focus_destination('vipirc')
                s.leave_app()
                s.wait(lambda:s.is_main(),'header round trip did not restore editor')
                m.selected = False
            elif op == 'clear':
                s.clear_input();m.clear()
            elif op == 'invalid':
                s.type(b'\xff\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80Q')
                m.insert(['Q'])
            trail.append((step, op, m.pos, len(m.text().encode('utf-8'))))
            operations += 1
            try:
                check_model(s, m, 'seed=%d step=%d op=%s' % (seed, step, op))
                if step and step % 53 == 0:
                    resize(s, *rng.choice(GEOMETRIES))
                    check_model(s, m, 'seeded resize changed state')
                if step and step % 127 == 0: verify_full_draft(s, m)
                if step % 100 == 0: samples.append({'step': step, 'rss_kib': rss_kib(s.proc.pid)})
            except AssertionError as error:
                raise AssertionError('%s\nrecent deterministic operations=%r' % (error, trail[-40:])) from error
        verify_full_draft(s, m)
        no_offline_send(s)
    return {'name': 'editor-' + locale + '-' + term + '-' + keymap,
            'seed': seed, 'operations': operations, 'seconds': round(time.monotonic() - started, 3),
            'rss_samples': samples, 'terminal_restored': True, 'unintended_sends': 0}


def modal_resize_churn(seed, rounds):
    rng = random.Random(seed)
    started = time.monotonic()
    samples = []
    transitions = 0
    with OfflineTerminal(rows=26, cols=132) as s:
        s.start();s.input_mode()
        draft = 'PRESERVED-DRAFT-' + 'abc123' * 12
        s.type(draft);wait_bytes(s,len(draft))
        for turn in range(rounds):
            resize(s, 26, 132)
            choice = turn % 6
            if choice == 0:
                s.navigate('w');s.wait(lambda:s.destination_visible('Mentions/unread'),'combined activity list missing')
                s.type(DOWN * rng.randint(0, 13) + UP * rng.randint(0, 13))
            elif choice == 1:
                s.navigate('?');s.wait(lambda:'Help / Manual' in ''.join(s.screen.cells[2]),'manual missing')
                s.type(b'\x1b[6~' * rng.randint(1, 5) + END + HOME)
            elif choice == 2:
                open_settings(s)
                index = rng.choice([0, 1, 5, HISTORY_FIELD])
                select_field(s,index);s.type(b'\r')
                s.wait(lambda:'Edit: ' in s.screen.text(),'Settings field missing')
                s.type(BEGIN_PASTE + b'data\t/quit\r\n\x1b' + END_PASTE)
            elif choice == 3:
                s.navigate('c');s.wait(lambda:re.match(r'^(?:\[OFFLINE\] )?Channels(?:\s*\||$)',s.panel_title()),'channels missing')
                s.type(DOWN * 15 + LEFT * 7 + RIGHT * 3)
            elif choice == 4:
                s.navigate('q');s.wait(lambda:'Query list' in ''.join(s.screen.cells[2]),'queries missing')
                s.type(DOWN * 12 + LEFT * 5 + RIGHT * 2)
            else:
                s.navigate('t')
                s.wait(lambda:in_transfers(s),'transfer list missing')
                transfer_action(s,'Send...')
                s.wait(lambda:'Send file' in s.screen.text(),'send form missing')
                s.type(b'\r')
                s.wait(lambda:'Edit: Nickname' in s.screen.text(),'send field missing')
                s.type(BEGIN_PASTE + b'NeverSend\t/quit\r\n\x1b' + END_PASTE)
            # A broad range includes below-minimum terminals and then valid
            # compact layouts. No hidden action is deliberately activated.
            for geometry in [rng.choice(GEOMETRIES),(5,10),(8,19),rng.choice(GEOMETRIES),(26,132)]:
                resize(s,*geometry);transitions += 1
                s.type(b'\x04\x01\x02\x18')
            s.type(b'\t')
            s.quiet(.025)
            assert s.header_focus() is None and not s.is_main(), 'panel Tab entered global navigation or chat'
            # Paste cannot activate menus or leave a field. In active editors
            # it inserts data only; the entire staged panel is cancelled next.
            s.type(BEGIN_PASTE + b'/quit\r\n\x1b\t' + END_PASTE)
            s.quiet(.025)
            return_chat(s)
            wait_bytes(s,len(draft))
            assert draft in s.screen.text(), 'modal/resize cycle changed draft'
            no_offline_send(s)
            s.input_mode()
            if turn % 6 == 0:samples.append({'round':turn,'rss_kib':rss_kib(s.proc.pid)})
        assert s.config.read_bytes()==s.before_config
    return {'name':'modal-resize-churn','seed':seed,'rounds':rounds,'geometry_transitions':transitions,
            'seconds':round(time.monotonic()-started,3),'rss_samples':samples,
            'terminal_restored':True,'unintended_sends':0}


def maximum_draft_cycles(seed, rounds):
    rng=random.Random(seed)
    started=time.monotonic()
    samples=[]
    total_bytes=0
    payloads=[b'A'*8192,('\u4e2d'*5461+'X').encode('utf-8'),
              ('\U0001f642'*4096).encode('utf-8')]
    with OfflineTerminal(rows=24,cols=100) as s:
        s.start();s.input_mode()
        for turn in range(rounds):
            payload=payloads[turn%len(payloads)]
            # Keep a paste open across redraws and extreme geometry changes.
            # Warning/confirmation must not consume or transmit its contents.
            split=rng.randint(1,len(payload)-1)
            s.type(BEGIN_PASTE+payload[:split])
            s.quiet(.025)
            assert WARNING not in s.screen.text(),'warning opened inside an unfinished paste'
            resize(s,*rng.choice(GEOMETRIES))
            s.type(payload[split:]+END_PASTE+b'y'*64+b'\t'*32)
            s.wait(lambda:WARNING in s.screen.text() or
                   re.search(r'\d+-byte limit',s.screen.text()) is not None,
                   'maximum draft did not show its length warning')
            assert s.proc.poll() is None
            resize(s,24,100)
            s.wait(lambda:WARNING in s.screen.text(),'resizing lost the active warning')
            s.quiet(.13);s.type(b'\x1b')
            s.wait(lambda:WARNING not in s.screen.text(),'warning did not acknowledge')
            wait_bytes(s,len(payload))
            # Beyond either storage bound, additional input must be rejected
            # without destroying any existing text, even after rewrapping.
            s.type(('\u4e2d'*12).encode('utf-8'));s.quiet(.03)
            wait_bytes(s,len(payload))
            resize(s,24,100)
            s.type(b'\r')
            s.wait(lambda:'Send all to ' in s.screen.text(),'multiline confirmation missing')
            s.type(BEGIN_PASTE+b'y\r\n/quit\t\x1b'+END_PASTE)
            s.quiet(.13)
            assert 'Send all to ' in s.screen.text(),'pasted data answered a send confirmation'
            s.type(b'\r')
            s.wait(lambda:'Send all to ' not in s.screen.text(),'default-No confirmation failed')
            wait_bytes(s,len(payload))
            # An invalid paste cannot destroy a full composition.
            s.type(BEGIN_PASTE+b'\xff\xc0\xaf'+END_PASTE);s.quiet(.025)
            wait_bytes(s,len(payload))
            replacement='cycle-%d-replacement'%turn
            s.clear_input()
            s.type(replacement)
            wait_bytes(s,len(replacement))
            assert replacement in s.screen.text(),'cleared draft did not accept new input'
            s.clear_input();wait_bytes(s,0)
            no_offline_send(s)
            total_bytes+=len(payload)
            samples.append({'round':turn,'rss_kib':rss_kib(s.proc.pid)})
    return {'name':'maximum-draft-cycles','seed':seed,'rounds':rounds,
            'accepted_paste_bytes':total_bytes,'seconds':round(time.monotonic()-started,3),
            'rss_samples':samples,'terminal_restored':True,'unintended_sends':0}


def server_draft_churn(seed, rounds):
    """Exercise all eight lazy UIs with identical channel names and own drafts."""
    rng=random.Random(seed)
    started=time.monotonic()
    expected={}
    samples=[]
    switches=0
    mouse_batches=0
    with MultiServerSession(offline=True,rows=26,cols=132) as s:
        s.start()
        for slot in range(1,8):
            actual,unused=s.add_server(nick='UiStress%d'%slot,channel='#test')
            assert actual==slot
        for slot in range(8):
            s.select_server(slot)
            if slot%2:
                open_settings(s,target=slot)
                edit_field(s,HISTORY_FIELD,"200")
                settings_action(s,1)
                s.wait(lambda:'Preferences applied' in s.screen.text(),'server history preference Apply failed')
                return_chat(s)
            style='brackets' if slot%2 else 'reverse'
            status_focus(s,'Channels',style)
            assert s.header_focus() is None and s.mode()=='PANEL' and s.destination_visible('Channels'), (
                'server category did not focus the Channels body',s.screen.text())
            s.input_mode()
            draft='slot-%d::'%slot+('private%d-'%slot)*4+'tail'
            position=7+slot
            s.type(draft);wait_bytes(s,len(draft))
            s.type(HOME+RIGHT*position)
            s.wait(lambda:editor_info(s).col==position+1,'initial session caret failed')
            s.navigate()
            s.input_mode()
            expected[slot]=[draft,position]
        samples.append({'switch':0,'rss_kib':rss_kib(s.proc.pid)})
        for turn in range(rounds):
            slot=rng.randrange(8)
            draft,position=expected[slot]
            if turn%3==0:
                clear_row=s.screen.rows-1
                open_servers(s)
                def matching_rows():
                    return [row for row in range(4,s.screen.rows-2)
                            if re.search(r'\|\s*\*?\s*%d:\s'%slot,''.join(s.screen.cells[row]))]
                s.wait(lambda:len(matching_rows())==1,
                       'requested mouse server row did not finish rendering: %d'%slot)
                rows=matching_rows()
                assert len(rows)==1,('server picker row missing',slot,s.screen.text())
                # A click selects a different session. The queued old-screen
                # coordinate would hit Clear on the destination main screen.
                s.type(click(22,rows[0])+click(2,clear_row))
                s.wait(lambda:not in_servers(s) and editor_info(s) is not None,
                       'mouse server switch did not return main')
                mouse_batches+=1
            else:s.select_server(slot)
            switches+=1
            info=wait_bytes(s,len(draft))
            assert info.col==position+1,('server switch lost caret',slot,position,info.col)
            assert draft in s.screen.text(),('server switch changed another draft',slot,s.screen.text())
            style='brackets' if slot%2 else 'reverse'
            status_focus(s,'Channels',style)
            assert s.header_focus() is None,'server category retained header focus after switch'
            assert s.mode()=='PANEL' and 'Channels' in s.panel_title()
            s.input_mode()
            # Insert in the middle, then return with either Delete or Backspace.
            # The caret must be restored exactly, not simply the byte count.
            marker=chr(ord('A')+(turn%26))
            s.type(marker);wait_bytes(s,len(draft)+1)
            changed=draft[:position]+marker+draft[position:]
            assert changed in s.screen.text(),'insertion used a stale session caret'
            if turn%2:s.type(LEFT+DELETE)
            else:s.type(b'\x7f')
            info=wait_bytes(s,len(draft))
            assert info.col==position+1 and draft in s.screen.text()
            # Move to a different logical offset for the next visit.
            position=rng.randrange(len(draft)+1)
            s.type(HOME+RIGHT*position)
            s.wait(lambda:editor_info(s).col==position+1,'session caret move failed')
            expected[slot][1]=position
            s.navigate()
            s.input_mode()
            if turn%17==0:
                resize(s,*rng.choice(GEOMETRIES));resize(s,26,132)
                info=wait_bytes(s,len(draft))
                assert info.col==position+1,'resize after switch lost caret'
            if turn%20==0:samples.append({'switch':switches,'rss_kib':rss_kib(s.proc.pid)})
        for slot,(draft,position) in expected.items():
            s.select_server(slot)
            info=wait_bytes(s,len(draft))
            assert info.col==position+1 and draft in s.screen.text()
        # No Enter ever submits a composition during this test. Simulator
        # activity continues in every connection, so a stale modal dispatch
        # must not manufacture a local chat echo for any of the eight nicks.
        nicks=['offline_first']+['UiStress%d'%slot for slot in range(1,8)]
        for nick in nicks:
            assert ('<'+nick+'>').encode('ascii') not in s.raw,'unintended chat on '+nick
        assert not s.data_home.exists()
    return {'name':'eight-server-draft-churn','seed':seed,'servers':8,'switches':switches,
            'mouse_batches':mouse_batches,'seconds':round(time.monotonic()-started,3),
            'rss_samples':samples,'terminal_restored':True,'unintended_sends':0}


def live_overlay_disconnects(seed):
    rng=random.Random(seed)
    started=time.monotonic()
    cases=['settings','settings-field','manual','channels','queries','transfers','transfer-field','warning']
    for kind in cases:
        with MultiServerSession(rows=26,cols=132) as s:
            s.start();slot,second=s.add_server(nick='StillConnected',channel='#test')
            s.select_server(0);s.command("/channel #test");s.input_mode()
            draft='retained-through-'+kind
            if kind=='warning':
                budget=editor_info(s).budget
                draft='W'*(budget+29)
                s.type(BEGIN_PASTE+draft.encode('ascii')+END_PASTE)
                s.wait(lambda:WARNING in s.screen.text(),'warning not opened before disconnect')
            else:
                s.type(draft);wait_bytes(s,len(draft))
                if kind.startswith('settings'):
                    open_settings(s)
                    if kind.endswith('field'):
                        select_field(s,5);s.type(b'\r')
                        s.wait(lambda:'Edit: Real name' in s.screen.text(),'real-name editor missing')
                        s.type(BEGIN_PASTE+b'unapplied field data'+END_PASTE)
                elif kind=='manual':
                    s.navigate('?');s.wait(lambda:'Help / Manual' in s.screen.text(),'manual missing')
                elif kind=='channels':
                    s.navigate('c');s.wait(lambda:re.match(r'^(?:\[OFFLINE\] )?Channels(?:\s*\||$)',s.panel_title()),'channels missing')
                elif kind=='queries':
                    s.navigate('q');s.wait(lambda:'Query list' in ''.join(s.screen.cells[2]),'queries missing')
                else:
                    s.navigate('t');s.wait(lambda:in_transfers(s),'transfers missing')
                    if kind.endswith('field'):
                        transfer_action(s,'Send...');s.type(b'\r')
                        s.wait(lambda:'Edit: Nickname' in s.screen.text(),'DCC field missing')
                        s.type('NeverOffered')
            # A real connection closes while an unrelated modal screen owns
            # input. A second connection must still service its PING promptly.
            s.server.send('ERROR :UI stress disconnect '+kind)
            marker='overlay-live-'+kind
            second.send('PING :'+marker)
            s.wait(lambda:any(line==('PONG :'+marker).encode('ascii') for unused,line in second.lines),
                   'secondary connection stopped servicing PING',timeout=3)
            resize(s,*rng.choice([(12,40),(24,80),(32,132)]))
            resize(s,26,132)
            if kind=='warning':
                s.quiet(.13);s.type(b'\x1b')
                s.wait(lambda:WARNING not in s.screen.text(),'disconnected warning did not close')
                assert s.mode()=='INPUT'
            else:return_chat(s)
            wait_bytes(s,len(draft))
            if kind!='warning':assert draft in s.screen.text()
            # Connection diagnostics live in the application view;
            # the main row identifies the conversation without repeating them.
            s.open_app_diagnostics()
            s.wait(lambda:'disconnected' in s.app_line('Connection:').lower(),
                   'closed connection was not reported by the live diagnostic')
            s.close_app_diagnostics()
            wait_bytes(s,len(draft))
            if kind!='warning':assert draft in s.screen.text()
            assert not any(line.startswith(b'PRIVMSG ') for p in s.peers for unused,line in p.lines),(
                'overlay/disconnect sent chat or a file offer',kind)
            assert not s.data_home.exists(),'overlay/disconnect opened transfer destination'
    return {'name':'live-overlay-disconnects','seed':seed,'cases':cases,
            'seconds':round(time.monotonic()-started,3),'terminal_restored':True,'unintended_sends':0}


def run_case(results, function, *args):
    result = function(*args)
    result['status'] = 'passed'
    results.append(result)
    print(json.dumps(result,sort_keys=True),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed',type=lambda text:int(text,0),default=20261027)
    parser.add_argument('--steps',type=int,default=1200)
    parser.add_argument('--rounds',type=int,default=48)
    parser.add_argument('--offline-only',action='store_true')
    parser.add_argument('--live-only',action='store_true')
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    if args.offline_only and args.live_only:parser.error('choose at most one mode restriction')
    if not 1<=args.steps<=100000 or not 1<=args.rounds<=10000:parser.error('steps/rounds out of bounds')
    report={'binary':str(BINARY),'binary_sha256':hashlib.sha256(BINARY.read_bytes()).hexdigest(),
            'seed':args.seed,'rss_method':'child /proc VmRSS KiB with PPid/NSpid identity verification','results':[]}
    started=time.monotonic()
    try:
        if not args.live_only:
            run_case(report['results'],mouse_transfer_boundaries,True)
            run_case(report['results'],mouse_view_settings_boundaries)
            run_case(report['results'],wide_caret_redraws)
            for offset,(locale,term,keymap) in enumerate([('C.UTF-8','xterm-256color','original'),
                                                         ('C.UTF-8','xterm-256color','vim'),
                                                         ('C','screen','original')]):
                run_case(report['results'],editor_fuzz,args.seed+offset,args.steps,locale,term,keymap)
            run_case(report['results'],modal_resize_churn,args.seed,args.rounds)
            run_case(report['results'],maximum_draft_cycles,args.seed,max(3,args.rounds//4))
            run_case(report['results'],server_draft_churn,args.seed,args.rounds*4)
        if not args.offline_only:
            run_case(report['results'],mouse_transfer_boundaries,False)
            run_case(report['results'],mouse_transfer_floods)
            run_case(report['results'],live_overlay_disconnects,args.seed)
        report['status']='passed'
    except Exception as error:
        report['status']='failed';report['error']=str(error)
        raise
    finally:
        report['elapsed_seconds']=round(time.monotonic()-started,3)
        if args.report:args.report.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='ascii')


if __name__=='__main__':main()
