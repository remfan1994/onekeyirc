#!/usr/bin/env python3
"""Application Use signals policy with a real shell foreground process group.

The launcher observes the actual client's stop/exit and records complete tty
attributes before returning control to the shell. It does not replace the
client's signal handlers or tty operations. No network or session IPC is used.
"""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import shlex
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

from test_idle_terminal import IdleScreen, IdleSession, LEFT


PROMPT = b"TERMINAL_POLICY_SHELL> "
REMAPPED = {"VINTR": 7, "VSUSP": 29, "VQUIT": 24}

# This process is the shell job leader and the client's parent. Keeping it in
# the foreground job avoids an orphaned process group, where POSIX job-control
# stops can be discarded. The wrapper catches terminal signals only for
# itself; caught dispositions reset when its child execs the real program.
LAUNCHER = r'''import fcntl
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import termios

spec_path, state_path, *command = sys.argv[1:]
state_path = Path(state_path)
spec = json.loads(Path(spec_path).read_text())
original = termios.tcgetattr(0)
assert bool(original[3] & termios.ISIG) == spec["isig"]
for key, value in spec["mappings"].items():
    inherited = original[6][getattr(termios, key)]
    inherited = inherited if isinstance(inherited, int) else inherited[0]
    assert inherited == value

def snapshot():
    attrs = termios.tcgetattr(0)
    attrs[6] = [value if isinstance(value, int) else value[0] for value in attrs[6]]
    return {"termios": attrs, "input_flags": fcntl.fcntl(0, fcntl.F_GETFL),
            "output_flags": fcntl.fcntl(1, fcntl.F_GETFL)}

def ignore(signum, frame):
    pass

for signum in (signal.SIGINT, signal.SIGQUIT, signal.SIGTSTP):
    signal.signal(signum, ignore)
# A failed SIGQUIT restoration must not leave a core file in a developer's
# working tree. The client still receives the real terminal-generated signal.
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
state = {"inherited": snapshot(), "stops": [], "wrapper_pid": os.getpid(),
         "job_pgid": os.getpgrp(), "session_id": os.getsid(0)}

def save():
    temporary = state_path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, sort_keys=True) + "\n")
    temporary.replace(state_path)

client = subprocess.Popen(command, close_fds=True)
state["client_pid"] = client.pid
state["client_pgid"] = os.getpgid(client.pid)
save()
try:
    while True:
        pid, status = os.waitpid(client.pid, os.WUNTRACED)
        assert pid == client.pid
        if os.WIFSTOPPED(status):
            state["stops"].append({"signal": os.WSTOPSIG(status), "tty": snapshot()})
            save()
            # The actual app has stopped after restoring its tty. Now let the
            # shell observe a stopped foreground job and handle ordinary fg.
            signal.signal(signal.SIGTSTP, signal.SIG_DFL)
            os.kill(os.getpid(), signal.SIGTSTP)
            signal.signal(signal.SIGTSTP, ignore)
            continue
        if os.WIFEXITED(status):
            result = os.WEXITSTATUS(status)
        else:
            assert os.WIFSIGNALED(status)
            result = 128 + os.WTERMSIG(status)
        client.returncode = result
        state["exit"] = {"status": result, "tty": snapshot()}
        save()
        break
finally:
    termios.tcsetattr(0, termios.TCSANOW, original)
sys.exit(result)
'''


class ShellSession:
    def __init__(self, binary, full, isig, mappings, override):
        self.temp = tempfile.TemporaryDirectory(prefix="irc-terminal-policy-")
        self.directory = Path(self.temp.name)
        self.state_path = self.directory / "state.json"
        self.config = self.directory / "config.ini"
        setting = "" if override is None else "use_signals=%s\n" % (
            "true" if override else "false")
        self.config.write_text(
            "[connection]\nserver=offline.invalid\nnick=tester\nchannel=#test\n"
            "[input]\ncomposer_mode=fullscreen\n" + setting)
        self.config.chmod(0o600)
        self.saved_config = self.config.read_bytes()
        launcher = self.directory / "launcher.py"
        launcher.write_text(LAUNCHER)
        spec = self.directory / "spec.json"
        spec.write_text(json.dumps({"isig": isig, "mappings": mappings}))
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 18, 100, 0, 0))
        self.before = termios.tcgetattr(self.slave)
        self.raw = bytearray()
        self.screen = IdleScreen(18, 100)
        self.finished = False
        self.original_stty = None
        self.stty_file = self.directory / "original-stty"
        self.policy = ["stty", "isig" if isig else "-isig"]
        if mappings:
            self.policy += ["intr", "^G", "susp", "^]", "quit", "^X"]
        env = dict(os.environ, TERM="xterm-256color", LC_ALL="C.UTF-8",
                   PS1=PROMPT.decode(), HOME=str(self.directory),
                   XDG_CONFIG_HOME=str(self.directory / "unused"))

        def child():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.shell = subprocess.Popen(
            ["bash", "--noprofile", "--norc", "-i"], stdin=self.slave,
            stdout=self.slave, stderr=self.slave, env=env, cwd=self.directory,
            preexec_fn=child, close_fds=True)
        os.set_blocking(self.master, False)
        command = [sys.executable, str(launcher), str(spec), str(self.state_path), str(binary)]
        if full:
            command.append("--idle")
        command += ["--test-mode", "--config", str(self.config)]
        self.command = " ".join(shlex.quote(part) for part in command).encode() + b"\n"

    def start(self):
        self.wait(lambda: PROMPT in self.raw, "interactive shell did not start")
        # Configure the actual parent shell as a user would with stty. Setting
        # mappings only inside a job would let bash restore its older mappings
        # on fg, testing a different inherited policy after the first stop.
        mark = len(self.raw)
        self.write(("stty -g > " + shlex.quote(str(self.stty_file)) + "\n").encode())
        self.wait(lambda: self.stty_file.exists() and PROMPT in self.raw[mark:],
                  "shell did not save its initial tty settings")
        self.original_stty = self.stty_file.read_text().strip()
        assert self.original_stty
        mark = len(self.raw)
        self.write(" ".join(shlex.quote(part) for part in self.policy).encode() + b"\n")
        self.wait(lambda: PROMPT in self.raw[mark:], "shell did not apply terminal policy")
        self.write(self.command)
        self.wait(lambda: self.state_path.exists(), "launcher did not record process identity")
        state = self.state()
        assert state["job_pgid"] == state["wrapper_pid"] == state["client_pgid"]
        assert state["session_id"] == self.shell.pid
        assert state["job_pgid"] != os.getpgrp() and state["job_pgid"] != self.shell.pid
        self.wait(lambda: b"Not vegetarian?" in self.raw, "client welcome did not appear")

    def state(self):
        return json.loads(self.state_path.read_text())

    def write(self, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        assert os.write(self.master, data) == len(data)

    def pump(self, timeout=.03):
        if not select.select([self.master], [], [], timeout)[0]:
            return
        try:
            data = os.read(self.master, 65536)
        except OSError as error:
            if error.errno in (errno.EIO, errno.EAGAIN):
                return
            raise
        self.raw.extend(data)
        self.screen.feed(data)

    def wait(self, predicate, message, timeout=4):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if predicate():
                return
            self.pump()
            assert self.shell.poll() is None, "interactive shell exited unexpectedly"
        raise AssertionError((message, bytes(self.raw[-1600:])))

    def quiet(self, seconds=.15):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.pump(max(0, min(.03, end - time.monotonic())))

    def active_policy(self, enabled, mappings):
        current = termios.tcgetattr(self.slave)
        assert bool(current[3] & termios.ISIG) == enabled, (
            "client changed inherited ISIG contrary to the selected policy", bool(current[3] & termios.ISIG), enabled)
        inherited = self.state()["inherited"]["termios"]
        for key in ("VINTR", "VSUSP", "VQUIT"):
            value = current[6][getattr(termios, key)]
            value = value if isinstance(value, int) else value[0]
            expected = mappings.get(key, inherited[6][getattr(termios, key)])
            assert value == expected, ("client remapped a terminal control", key, value, expected)

    def inert_default_controls(self, draft=None, caret=None):
        state = self.state()
        stops = len(state["stops"])
        self.write(b"\x03\x1a\x1c")
        self.quiet()
        after = self.state()
        assert "exit" not in after and len(after["stops"]) == stops, (
            "literal default signal bytes exited or suspended the client", after)
        os.kill(after["client_pid"], 0)
        if draft is None:
            assert b"joined #test" not in self.raw, "literal control bytes dismissed the welcome"
        else:
            assert IdleSession.editor(self, draft, len(draft)), "literal control bytes changed the draft"
            assert (self.screen.row, self.screen.col) == caret, "literal control bytes moved the caret"

    def cancel_default_controls(self):
        """With signals disabled, Ctrl-C/Z cancel (never signal or send)."""
        for key, draft in ((3, "mapped-signal-draft"), (26, "second-unsent-draft")):
            if key == 26:
                self.write(draft)
                self.wait(lambda: IdleSession.editor(self, draft, len(draft)),
                          "second draft did not enter Compose")
            marker = len(self.raw)
            self.write(bytes((key,)))
            self.wait(lambda: b"Draft cancelled" in self.raw[marker:] and not self.screen.alternate,
                      "Ctrl-C/Z did not cancel the draft")
            assert "exit" not in self.state() and not self.state()["stops"],                 "Ctrl-C/Z terminated or suspended the client"

    def suspend(self, byte, welcome=False, draft=None, caret=None):
        stop_count = len(self.state()["stops"])
        mark = len(self.raw)
        self.write(bytes((byte,)))
        self.wait(lambda: len(self.state()["stops"]) == stop_count + 1,
                  "mapped terminal suspend did not stop the client")
        state = self.state()
        stopped = state["stops"][-1]
        assert stopped["signal"] == signal.SIGTSTP
        assert stopped["tty"] == state["inherited"], "app did not restore its exact tty before stopping"
        self.wait(lambda: b"Stopped" in self.raw[mark:] and PROMPT in self.raw[mark:],
                  "real shell did not recover the stopped foreground job")
        mark = len(self.raw)
        self.write(b"fg\n")
        if welcome:
            self.wait(lambda: b"Not vegetarian?" in self.raw[mark:], "fg did not restore welcome")
        else:
            self.wait(lambda: IdleSession.editor(self, draft, len(draft)), "fg lost the retained draft")
            assert (self.screen.row, self.screen.col) == caret, "fg moved the retained caret"

    def start_draft(self):
        self.write(b"x")
        self.wait(lambda: b"joined #test" in self.raw, "offline client did not start")
        draft = "mapped-signal-draft"
        self.write(draft)
        self.wait(lambda: IdleSession.editor(self, draft, len(draft)), "draft was not composed")
        end = self.screen.row, self.screen.col
        self.write(LEFT * 3)
        self.wait(lambda: (self.screen.row, self.screen.col) == (end[0], end[1] - 3),
                  "caret did not move left within the draft")
        return draft, (self.screen.row, self.screen.col)

    def finish(self, status, byte=None):
        mark = len(self.raw)
        if byte is None:
            os.kill(self.state()["client_pid"], signal.SIGTERM)
        else:
            self.write(bytes((byte,)))
        self.wait(lambda: "exit" in self.state(), "mapped terminal signal did not terminate the client")
        state = self.state()
        assert state["exit"]["status"] == status, state["exit"]
        assert state["exit"]["tty"] == state["inherited"], "app did not restore its exact tty before exiting"
        self.wait(lambda: PROMPT in self.raw[mark:], "shell did not recover after client exit")
        self.finished = True
        return state

    def close(self):
        try:
            if not self.finished and self.state_path.exists():
                state = self.state()
                if "exit" not in state:
                    group = state["job_pgid"]
                    assert group not in (os.getpgrp(), self.shell.pid)
                    try:
                        os.killpg(group, signal.SIGCONT)
                        os.killpg(group, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    # Only this private job group is eligible for forced cleanup.
                    self.quiet(.1)
                    try:
                        os.killpg(group, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            if self.shell.poll() is None:
                if self.original_stty:
                    mark = len(self.raw)
                    self.write(("stty " + shlex.quote(self.original_stty) + "\n").encode())
                    self.wait(lambda: PROMPT in self.raw[mark:], "shell did not restore initial tty policy")
                self.write(b"exit\n")
                try:
                    self.shell.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.shell.kill()
                    self.shell.wait(timeout=2)
            if self.finished:
                assert termios.tcgetattr(self.slave) == self.before, "shell/job lifecycle changed the original tty"
            assert self.config.read_bytes() == self.saved_config, "signal test changed saved configuration"
        finally:
            if self.shell.poll() is None:
                self.shell.kill()
                self.shell.wait(timeout=2)
            os.close(self.master)
            os.close(self.slave)
            self.temp.cleanup()


def policy_case(binary, full, name):
    inherited = name != "inherited_disabled"
    override = True if name == "override_enabled" else None
    mappings = REMAPPED if inherited else {}
    session = None
    try:
        session = ShellSession(binary, full, inherited, mappings, override)
        session.start()
        # The default is OFF even when the invoking shell had ISIG enabled.
        active = bool(override)
        session.active_policy(active, mappings)
        session.inert_default_controls()  # Before the welcome screen dismisses.
        if active:
            session.suspend(REMAPPED["VSUSP"], welcome=True)
            session.active_policy(active, mappings)
        draft, caret = session.start_draft()
        session.active_policy(active, mappings)
        if active:
            session.inert_default_controls(draft, caret)
            session.suspend(REMAPPED["VSUSP"], draft=draft, caret=caret)
            session.active_policy(active, mappings)
            status = 131
            exit_byte = REMAPPED["VQUIT"]
        else:
            session.cancel_default_controls()
            status = 143
            exit_byte = None
        state = session.finish(status, exit_byte)
        result = {"case": name, "result": "PASS", "inherited_isig": inherited,
                  "use_signals": override, "active_isig": active,
                  "terminal_mappings": mappings or "inherited unchanged",
                  "default_ctrl_c_z_cancelled_draft": not active,
                  "real_sigtstp_stops": len(state["stops"]), "exit_status": status,
                  "exact_pre_shell_restorations": len(state["stops"]) + 1,
                  "real_foreground_job_group": True}
    except Exception as error:
        print(json.dumps({"case": name, "result": "FAIL", "error": str(error)}, sort_keys=True), flush=True)
        raise
    finally:
        if session is not None:
            session.close()
    print(json.dumps(result, sort_keys=True), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--full", action="store_true", help="exercise vipirc --idle")
    parser.add_argument("--case", choices=("inherited_disabled", "inherited_remapped", "override_enabled"))
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK), binary
    cases = [args.case] if args.case else ("inherited_disabled", "inherited_remapped", "override_enabled")
    for name in cases:
        policy_case(binary, args.full, name)
    print(json.dumps({"result": "PASS", "cases": len(cases),
                      "frontend": "vipirc -i" if args.full else "onekeyirc"}, sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
