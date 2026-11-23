#!/usr/bin/env python3
"""Socket-free regressions for held Ctrl-D, startup EOF and shell fall-through."""
import fcntl
import os
from pathlib import Path
import pty
import pwd
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

from test_integration import ModeControls, Screen
from test_offline import BINARY, OfflineTerminal
from test_input_limits import WARNING, acknowledge, assert_no_send, editor_info, wait_bytes


PROMPT = b"VIPIRC_CTRL_D_PARENT> "
DOWN, HOME = b"\x1bOB", b"\x1bOH"


def repeat_ctrl_d(session, label, check=None):
    # A held key produces both queued bursts and separate reads. Exercise
    # both, rather than sending one EOF and immediately another valid key.
    for unused in range(8):
        session.type(b"\x04" * 8)
        session.quiet(0.015)
        assert session.proc.poll() is None, label + ": Ctrl-D exited the process"
        if check is not None:
            check()
    session.quiet(0.04)


def secondary(session, title):
    return title in session.panel_title()


def main_draft(session, expected):
    info = wait_bytes(session, len(expected.encode("utf-8")))
    visible = "\n".join("".join(row[1:-1]).rstrip() for row in session.screen.cells[info.label_row + 1:-2])
    assert expected in visible, ("Ctrl-D changed the draft", expected, visible)
    assert_no_send(session)


def application_controls():
    with OfflineTerminal() as session:
        session.wait(lambda: b"Not vegetarian?" in session.raw, "welcome missing")
        repeat_ctrl_d(session, "welcome")
        assert b"OFFLINE TEST MODE" not in session.raw, "Ctrl-D dismissed the welcome"
        session.start()
        repeat_ctrl_d(session, "empty main editor")
        assert session.is_main() and session.mode() == "INPUT"
        wait_bytes(session, 0)

        session.focus_destination("vipirc")
        repeat_ctrl_d(session, "empty selected header")
        assert "F1 vipirc" in "".join(session.screen.cells[0]) and session.header_focus() is None, "Ctrl-D changed category/body focus"
        assert "vipirc" in session.panel_title(), "Ctrl-D changed the opened app destination"
        session.leave_app()
        session.wait(lambda:session.is_main(),"header Escape did not restore chat")
        session.input_mode()
        repeat_ctrl_d(session, "empty input")
        assert session.mode() == "INPUT"
        wait_bytes(session, 0)

        draft = "draft retained after Ctrl-D"
        session.type(draft)
        main_draft(session, draft)
        repeat_ctrl_d(session, "nonempty input")
        main_draft(session, draft)
        assert session.mode() == "INPUT"

        # Byte4 is ignored before decoding; it must not damage a partially
        # received UTF-8 character, including on an SSH-style slow stream.
        session.type(b"\xc3")
        session.quiet(0.03)
        repeat_ctrl_d(session, "fragmented UTF-8 input")
        session.type(b"\xa9")
        draft += "\u00e9"
        main_draft(session, draft)
        session.focus_destination("vipirc")
        repeat_ctrl_d(session, "nonempty selected header")
        assert "F1 vipirc" in "".join(session.screen.cells[0]) and session.header_focus() is None, "Ctrl-D changed category/body focus"
        assert "vipirc" in session.panel_title(), "Ctrl-D changed the opened app destination over a draft"
        session.leave_app()
        session.wait(lambda:session.is_main(),"header Escape did not restore retained draft")
        main_draft(session, draft)

        for action, title in (("?", "Help / Manual"), ("c", "Channels"), ("q", "Query list")):
            session.navigate(action)
            session.wait(lambda: secondary(session, title), title + " did not open")
            repeat_ctrl_d(session, title)
            assert secondary(session, title), "Ctrl-D left " + title
            assert_no_send(session)
            session.type(b"\x1b")
            session.wait(lambda: not secondary(session, title), title + " did not return to main")
            if action=="?":
                session.wait(lambda:session.panel_title().startswith("vipirc |"),"manual Escape did not return to vipirc")
                session.leave_app()
            main_draft(session, draft)

        from test_settings import open_settings
        open_settings(session)
        session.wait(lambda: secondary(session, "Settings"), "Settings did not open")
        repeat_ctrl_d(session, "Settings")
        assert secondary(session, "Settings")
        session.type(HOME + DOWN * 5 + b"\r")
        session.wait(lambda: "Edit: Real name" in session.screen.text(), "Settings text editor missing")
        repeat_ctrl_d(session, "Settings nonempty field")
        assert "Edit: Real name" in session.screen.text()
        assert "Offline configuration test" in session.screen.text()
        session.type(b"\x7f")  # Clear the initially selected field, not chat.
        session.quiet(0.04)
        repeat_ctrl_d(session, "Settings empty field")
        assert "Edit: Real name" in session.screen.text()
        session.type(b"\x1b")
        session.wait(lambda: session.panel_title().startswith("vipirc |"), "Settings did not return to vipirc")
        session.leave_app()
        session.wait(session.main_frame_ready,"vipirc did not restore the draft after Settings")
        main_draft(session, draft)

        session.clear_input()
        budget = editor_info(session).budget
        session.type("z" * budget)
        session.wait(lambda: WARNING in session.screen.text(), "early input warning missing")
        repeat_ctrl_d(session, "message warning")
        assert WARNING in session.screen.text(), "Ctrl-D acknowledged a warning"
        wait_bytes(session, budget)
        assert_no_send(session)
        acknowledge(session, b"\x1b")
    print("controls: held Ctrl-D is inert in welcome, empty/nonempty main input, selected headers, overlays, fields and warnings; UTF-8/drafts survive")


class PromptTerminal(ModeControls):
    # Reuse only the PTY byte pump and terminal model, never a socket fixture.
    type = OfflineTerminal.type
    pump = OfflineTerminal.pump
    quiet = OfflineTerminal.quiet
    wait = OfflineTerminal.wait

    def __init__(self, *, shell=True, eof=b"\x04", ignore_hangup=False):
        self.temp = tempfile.TemporaryDirectory(prefix="vipirc-controls-")
        self.directory = Path(self.temp.name)
        self.config = self.directory / "config.ini"
        self.config.write_bytes(b"")
        self.config.chmod(0o600)
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
        attrs = termios.tcgetattr(self.slave)
        attrs[6][termios.VEOF] = eof
        termios.tcsetattr(self.slave, termios.TCSANOW, attrs)
        self.screen = Screen(24, 100)
        self.raw = bytearray()
        self.client_group = None
        self.shell = shell
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8",
                   HOME=self.temp.name, XDG_CONFIG_HOME=str(self.directory / "unused"),
                   PS1=PROMPT.decode("ascii"), PS2="MORE> ", ENV="", BASH_ENV="", INPUTRC="/dev/null")
        # The shell must retain its ordinary EOF behavior: masking EOF in
        # the parent would hide precisely the terminal-tab regression.
        for name in ("IGNOREEOF", "SHELLOPTS", "BASHOPTS"):
            env.pop(name, None)
        application = [str(BINARY), "--no-connect", "--plain", "--config", str(self.config),
                       "--server", "prompt-no-network.invalid"]
        if shell:
            bash = shutil.which("bash")
            command = [bash, "--noprofile", "--norc", "-i"] if bash else ["/bin/sh", "-i"]
        else:
            command = application

        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)
            if ignore_hangup:
                signal.signal(signal.SIGHUP, signal.SIG_IGN)

        self.proc = subprocess.Popen(command, stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                     cwd=self.directory, env=env, preexec_fn=controlling_terminal)
        os.set_blocking(self.master, False)
        if shell:
            self.wait(lambda: PROMPT in self.raw, "interactive parent shell did not start")
            self.type("set -m\n")
            self.quiet(0.04)
            self.type(" ".join(shlex.quote(value) for value in application) + "\n")
        self.wait(lambda: b"Not vegetarian?" in self.raw, "normal startup welcome missing")
        if shell:
            self.client_group = os.tcgetpgrp(self.master)
            assert self.client_group != self.proc.pid, "client is not a foreground shell job"

    def assert_foreground(self):
        assert self.proc.poll() is None, "Ctrl-D terminated the interactive parent shell"
        if self.shell:
            assert os.tcgetpgrp(self.master) == self.client_group, "Ctrl-D fell through to the parent shell"
            os.kill(self.client_group, 0)

    def finish(self):
        begin = len(self.raw)
        self.type(b"\x03")
        if self.shell:
            self.wait(lambda: os.tcgetpgrp(self.master) == self.proc.pid and PROMPT in self.raw[begin:],
                      "explicit app interrupt did not return to the still-running shell")
            self.client_group = None
            self.type("printf 'VIPIRC_PARENT_%s\\n' 'STILL_ALIVE'\n")
            self.wait(lambda: b"VIPIRC_PARENT_STILL_ALIVE" in self.raw[begin:],
                      "parent shell was not usable after explicit app exit")
            self.type("exit\n")
        self.proc.wait(timeout=3)
        assert self.proc.returncode == 0, self.proc.returncode
        assert self.config.read_bytes() == b"", "prompting unexpectedly saved a configuration"

    def close(self):
        if self.client_group:
            try: os.killpg(self.client_group, signal.SIGKILL)
            except ProcessLookupError: pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try: self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=2)
        if self.master is not None:
            os.close(self.master)
        os.close(self.slave)
        self.temp.cleanup()

    def __enter__(self): return self
    def __exit__(self, kind, value, traceback): self.close()


def startup_prompts_and_parent():
    from test_settings import open_settings
    login = pwd.getpwuid(os.getuid()).pw_name
    valid_login = re.fullmatch(r"[A-Za-z\[\]\\`_^{}|][A-Za-z0-9\[\]\\`_^{}|\-]*", login) is not None
    for eof in (b"\x04", b"\x05"):
        with PromptTerminal(eof=eof) as session:
            repeat_ctrl_d(session, "parented welcome", session.assert_foreground)
            assert b"Set IRC server:" not in session.raw
            session.type("x")
            session.wait(lambda: b"Set user nick:" in session.raw, "nickname prompt missing for supplied server")
            repeat_ctrl_d(session, "empty nickname prompt", session.assert_foreground)
            assert session.mode() is None, "Ctrl-D accepted the default nickname without Enter"
            if eof == b"\x05" and valid_login:
                expected_nick = login
                assert ("Set user nick: (%s)" % login).encode("ascii") in session.raw
                session.type(b"\r")
            else:
                expected_nick = "controls_tester"
                session.type("controls_")
                repeat_ctrl_d(session, "partial nickname prompt", session.assert_foreground)
                assert session.mode() is None
                session.type("tester\r")
            session.wait(lambda: session.is_main() and session.mode() == "INPUT", "prompts did not enter the main editor")
            assert b"input is too long" not in session.raw
            assert b"Choose a valid IRC nickname" not in session.raw
            assert b"Set IRC server:" not in session.raw
            assert session.raw.count(b"Set user nick:") == 1
            open_settings(session)
            session.wait(lambda: secondary(session, "Settings"), "normal settings did not open")
            assert "Server: prompt-no-network.invalid" in session.screen.text()
            assert "Nickname: " + expected_nick in session.screen.text()
            session.type(b"\x1b")
            session.wait(lambda: not secondary(session, "Settings"), "normal settings did not close")
            session.input_mode()
            repeat_ctrl_d(session, "parented empty input", session.assert_foreground)
            wait_bytes(session, 0)
            session.finish()
    print("controls: real shell survives held EOF at empty/partial nickname prompts and input; text/default nick require Enter even with remapped VEOF")


def actual_hangup():
    with PromptTerminal(shell=False, ignore_hangup=True) as session:
        session.type("x")
        session.wait(lambda: b"Set user nick:" in session.raw, "hangup test nickname prompt missing")
        repeat_ctrl_d(session, "pre-hangup prompt")
        os.close(session.master)
        session.master = None
        session.proc.wait(timeout=2)
        assert session.proc.returncode == 0, ("hangup did not exit cleanly", session.proc.returncode)
    print("controls: a real terminal hangup exits promptly instead of retrying EOF forever")


def idle_resize_before_poll():
    """A real SIGWINCH delivered before poll must not wait for a user's key."""
    if not sys.platform.startswith("linux"):
        print("controls: pre-poll resize injection skipped (Linux ELF interposer); portable resize cases remain in input/status suites")
        return
    source = Path(__file__).with_name("prepoll_resize.c")
    with tempfile.TemporaryDirectory(prefix="vipirc-idle-resize-") as name:
        directory = Path(name)
        wrapper, marker, trace = directory / "poll.so", directory / "trigger", directory / "trace"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-O2", "-fPIC", "-shared",
                                  str(source), "-o", str(wrapper), "-ldl"], check=True, timeout=30,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # With a dynamically linked ASan binary its existing runtime must stay
        # first in the preload list. Keep sanitizer link-order checks enabled.
        libraries = subprocess.check_output(["ldd", str(BINARY)], text=True, timeout=5)
        asan = re.search(r"^\s*(?:libasan\.so\S*|libclang_rt\.asan\S*)\s+=>\s+(/\S+)", libraries, re.MULTILINE)
        preload = ([asan[1]] if asan else []) + [str(wrapper)]
        if os.environ.get("LD_PRELOAD"): preload.append(os.environ["LD_PRELOAD"])
        config = directory / "config.ini"
        config.write_bytes(b""); config.chmod(0o600)
        master, slave = pty.openpty()
        original_term = termios.tcgetattr(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8",
                   LD_PRELOAD=":".join(preload), VIPIRC_PREPOLL_MARKER=str(marker), VIPIRC_PREPOLL_LOG=str(trace))
        def controlling_terminal():
            os.setsid(); fcntl.ioctl(0, termios.TIOCSCTTY, 0)
        process = subprocess.Popen([str(BINARY), "--config", str(config), "--server", "127.0.0.1",
                                    "--nick", "tester", "--no-connect"], stdin=slave, stdout=slave,
                                   stderr=slave, env=env, preexec_fn=controlling_terminal)
        os.close(slave); os.set_blocking(master, False)
        screen, raw = Screen(), bytearray()
        def pump(timeout=.03):
            import errno, select
            if select.select([master], [], [], timeout)[0]:
                try: data = os.read(master, 65536)
                except OSError as error:
                    if error.errno in (errno.EAGAIN, errno.EIO): return
                    raise
                raw.extend(data); screen.feed(data)
        def wait(predicate, label, timeout=5):
            until = time.monotonic() + timeout
            while time.monotonic() < until:
                pump()
                if predicate(): return
                if process.poll() is not None: break
            raise AssertionError("%s\nexit=%r\nscreen:\n%s\nrecent:%r" %
                                 (label, process.poll(), screen.text(), bytes(raw[-1200:])))
        def quiet(seconds):
            until = time.monotonic() + seconds
            while time.monotonic() < until: pump(min(.03, max(0, until-time.monotonic())))
        try:
            wait(lambda: b"Not vegetarian?" in raw, "idle-resize welcome missing")
            os.write(master, b"x")
            wait(lambda: "Channel 0: Server" in screen.text(), "disconnected chat missing")
            draft = "IDLE_RESIZE_DRAFT"
            os.write(master, draft.encode())
            wait(lambda: draft in screen.text(), "held idle draft missing")
            quiet(.2)
            marker.write_bytes(b"resize\n"); marker.chmod(0o600)
            wait(lambda: trace.exists() and "pending_after_unblock=0" in trace.read_text(),
                 "pre-poll resize seam did not fire", timeout=4)
            trigger = trace.read_text()
            assert "ioctl=0 pending_while_blocked=1 pending_after_unblock=0 timeout=1000" in trigger, trigger
            assert struct.unpack("HHHH", fcntl.ioctl(master, termios.TIOCGWINSZ, b"\0"*8))[:2] == (15, 50)
            # This is a real geometry change, not an unsupported same-size
            # SIGWINCH used to reconstruct a discarded virtual screen model.
            screen = Screen(15, 50, cursor_visible=screen.cursor_visible)
            observed_at = time.monotonic()
            wait(lambda: draft in screen.text() and "Clear" in "".join(screen.cells[14]),
                 "idle resize required a keyboard/network wake-up", timeout=1.8)
            redraw_ms = (time.monotonic() - observed_at) * 1000
            assert screen.cursor_visible, "resize hid the typing cursor"
            assert screen.col == len(draft) + 1 and 1 < screen.row < 13, screen.text()
            assert screen.reverse_cells[screen.row][screen.col], "resize lost the explicit caret cell"
            quiet(max(0, 3.2 - (time.monotonic() - observed_at)))
            assert process.poll() is None and draft in screen.text()
            os.write(master, b"\x03"); process.wait(timeout=3); pump(0)
            assert process.returncode == 130
            restored = termios.tcgetattr(master)
            mask = termios.ISIG | termios.ICANON | termios.ECHO
            assert restored[3] & mask == original_term[3] & mask
            assert b"\x1b[?2004l" in raw and config.read_bytes() == b""
            record = trace.read_text()
            assert "real_poll_result=0 stdin_revents=0" in record, record
            work = re.search(r"poll_calls_after_marker=(\d+) cpu_seconds=([\d.]+)", record)
            assert work and int(work[1]) <= 64 and float(work[2]) < .5, ("idle polling spun", record)
            print("controls: pre-poll SIGWINCH redraw %.1fms without keyboard/network; draft/caret, bounded idle CPU (%ss/%s polls) and terminal cleanup passed" %
                  (redraw_ms, work[2], work[1]))
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
            os.close(master)


def main():
    application_controls()
    startup_prompts_and_parent()
    actual_hangup()
    idle_resize_before_poll()
    print("all Ctrl-D / EOF control regressions passed")


if __name__ == "__main__":
    main()
