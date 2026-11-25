#!/usr/bin/env python3
"""CLI and offline simulation tests. This test opens no network sockets."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import stat
import struct
import subprocess
import tempfile
import termios
import time

# Only the VT screen model is reused; no IRCFixture or live Session is created.
from test_integration import ModeControls, Screen

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("VIPIRC_BINARY", str(ROOT / "vipirc")))


def cli_options():
    source_versions = re.findall(rb'^#define VIPIRC_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"$',
                                 (ROOT / "src/app.h").read_bytes(), re.MULTILINE)
    assert len(source_versions) == 1, "expected one explicit VIPIRC_VERSION definition"
    version_prefix = b"vipirc " + source_versions[0] + b"\n"
    with tempfile.TemporaryDirectory(prefix="vipirc-cli-") as name:
        directory = Path(name)
        env = dict(os.environ, XDG_CONFIG_HOME=str(directory / "unused-config"))
        missing = str(directory / "does-not-exist.ini")

        def run(*args, run_env=None):
            return subprocess.run([str(BINARY), *args], stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  cwd=directory, env=env if run_env is None else run_env, timeout=3)

        help_runs = [run("--config", missing, flag) for flag in ("-h", "--help")]
        for result in help_runs:
            assert result.returncode == 0, (result.returncode, result.stderr)
            assert result.stderr == b""
            assert b"Usage: vipirc" in result.stdout and b"--test-mode" in result.stdout
            assert b"--server" in result.stdout and b"--channel" in result.stdout
        assert help_runs[0].stdout == help_runs[1].stdout

        versions = [run("--config", missing, flag) for flag in ("-v", "-V", "--version")]
        for result in versions:
            assert result.returncode == 0, (result.returncode, result.stderr)
            assert result.stderr == b""
            assert result.stdout.startswith(version_prefix), result.stdout
        assert len({result.stdout for result in versions}) == 1
        for result in help_runs + versions:
            for attribution in (b"ThatIAm.vip", b"www.thatiam.vip", b"remfan1994", b"IALA"):
                assert attribution in result.stdout, (attribution, result.stdout)

        manual = run("--config", missing, "--manual")
        assert manual.returncode == 0 and manual.stderr == b"", (manual.returncode, manual.stderr)
        assert manual.stdout == (ROOT / "doc/manual.txt").read_bytes(), "built-in manual is stale"

        no_home = dict(env)
        no_home.pop("HOME", None)
        no_home.pop("XDG_CONFIG_HOME", None)
        for flag in ("-h", "--help", "-v", "-V", "--version", "--manual"):
            result = run(flag, run_env=no_home)
            expected = (manual.stdout if flag == "--manual" else
                        help_runs[0].stdout if flag in ("-h", "--help") else versions[0].stdout)
            assert result.returncode == 0 and result.stderr == b"" and result.stdout == expected, (
                "informational option depended on home/config discovery", flag, result.returncode, result.stderr)

        for args in (("--not-an-option",), ("--server",), ("--port", "0"), ("unexpected-operand",)):
            result = run(*args)
            assert result.returncode == 2, (args, result.returncode, result.stdout, result.stderr)
            assert result.stderr and not result.stdout
        missing_run = run("--test-mode", "--config", missing)
        assert missing_run.returncode == 2 and b"configuration" in missing_run.stderr.lower()
        for flag in ("--init-config", "--check-config"):
            incompatible = run("--test-mode", flag, "--config", missing)
            assert incompatible.returncode == 2 and incompatible.stderr, (flag, incompatible.stdout, incompatible.stderr)
        assert not list(directory.iterdir()), "CLI informational/error commands created files"
    print("offline: -h/--help, source-matched -v/-V/--version, attribution, exact manual, nonterminal operation, invalid arguments and missing-config errors passed")


class OfflineTerminal(ModeControls):
    def __init__(self, configured=True, no_connect=False, *, rows=24, cols=100,
                 term="xterm-256color", locale="C.UTF-8", keymap=None, focus_style=None,
                 config_extra="", test_mode=True):
        self.home_key = b"\x1b[1~" if term == "screen" else b"\x1bOH"
        self.no_connect = no_connect
        self.temp = tempfile.TemporaryDirectory(prefix="vipirc-offline-")
        self.directory = Path(self.temp.name)
        self.config = self.directory / "config.ini"
        self.before_config = None
        args = [str(BINARY)]
        if test_mode: args.append("--test-mode")
        if no_connect: args.append("--no-connect")
        if configured:
            secret = self.directory / "secret-must-not-be-read"
            os.mkfifo(secret, 0o600)
            self.before_config = (
                "[connection]\n"
                "server=must-not-resolve.invalid\n"
                "port=6697\n"
                "tls=true\n"
                "nick=offline_tester\n"
                "username=offline_user\n"
                "realname=Offline configuration test\n"
                "channel=#offline-a,#offline-b\n"
                "autoconnect=false\n"
                "auth=sasl\n"
                "auth_user=must-not-authenticate\n"
                "password_file=%s\n"
                "server_password_file=%s\n"
                "[interface]\n"
                "user_list=false\n"
                "history_lines=50\n"
                "%s"
                "%s"
                "[mentions]\n"
                "word=custom-watch-phrase\n" % (secret, secret,
                                               "keymap=%s\n" % keymap if keymap is not None else "",
                                               "focus_style=%s\n" % focus_style if focus_style is not None else "")
            ).encode("ascii")
            self.before_config += config_extra.encode("utf-8")
            self.config.write_bytes(self.before_config)
            self.config.chmod(0o600)
            args += ["--config", str(self.config)]
        self.before_tree = self.tree()
        self.master, slave = pty.openpty()
        self.term_before = termios.tcgetattr(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen = Screen(rows, cols)
        self.raw = bytearray()
        env = dict(os.environ, TERM=term, LANG=locale, LC_ALL=locale,
                   XDG_CONFIG_HOME=str(self.directory / "unused-config"))

        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.proc = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave,
                                     cwd=self.directory, env=env, preexec_fn=controlling_terminal)
        os.close(slave)
        os.set_blocking(self.master, False)

    def tree(self):
        return sorted((str(p.relative_to(self.directory)), stat.S_IFMT(p.lstat().st_mode),
                       stat.S_IMODE(p.lstat().st_mode), p.lstat().st_size)
                      for p in self.directory.rglob("*"))

    def pump(self, timeout=0.03):
        if not select.select([self.master], [], [], timeout)[0]: return
        try:
            data = os.read(self.master, 65536)
        except OSError as error:
            if error.errno in (errno.EIO, errno.EAGAIN): return
            raise
        self.raw.extend(data)
        self.screen.feed(data)

    def wait(self, predicate, label, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if predicate(): return
            if self.proc.poll() is not None: break
        raise AssertionError("%s\nexit=%r\nscreen:\n%s\nrecent output:%r" %
                             (label, self.proc.poll(), self.screen.text(), bytes(self.raw[-1500:])))

    def quiet(self, duration=0.15):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            self.pump(min(0.03, max(0, deadline - time.monotonic())))

    def type(self, data):
        if isinstance(data, str): data = data.encode("utf-8")
        deadline = time.monotonic() + 3
        while data:
            try: count = os.write(self.master, data)
            except BlockingIOError:
                assert time.monotonic() < deadline
                self.pump(0.01)
                continue
            data = data[count:]

    def start(self):
        self.wait(lambda: b"Not vegetarian?" in self.raw, "offline welcome missing")
        self.type("x")
        self.wait(lambda: "test mode" in self.screen.text().lower() or "simulation" in self.screen.text().lower() or
                  ("[OFFLINE]" in self.screen.text() and
                   ("Channel 0:" if self.no_connect else "Channel 1:") in self.screen.text()),
                  "offline simulation did not start")
        self.quiet()
        assert b"Set user nick:" not in self.raw and b"Set IRC server:" not in self.raw
        assert self.mode() == "INPUT", "offline startup must leave chat input active"
        assert "original" not in "".join(self.screen.cells[2]) and "vim" not in "".join(self.screen.cells[2]), (
            "startup exposed a removed navigation preset", self.screen.text())

    def close(self):
        forced = False
        interrupted = self.proc.poll() is None
        if interrupted:
            self.type(b"\x03")
            try: self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                forced = True
                self.proc.terminate()
                try: self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired: self.proc.kill(); self.proc.wait(timeout=2)
        self.pump(0)
        restored = termios.tcgetattr(self.master)
        os.close(self.master)
        try:
            assert not forced, "Ctrl-C failed to exit offline mode"
            assert self.proc.returncode == 130 if interrupted else self.proc.returncode in (0, 130), self.proc.returncode
            mask = termios.ISIG | termios.ICANON | termios.ECHO
            assert restored[3] & mask == self.term_before[3] & mask
            assert b"\x1b[?2004l" in self.raw
            assert self.tree() == self.before_tree, "offline mode created or modified files"
            if self.before_config is not None:
                assert self.config.read_bytes() == self.before_config, "offline mode rewrote configuration"
        finally:
            self.temp.cleanup()

    def __enter__(self): return self
    def __exit__(self, kind, value, traceback): self.close()


def configured_simulation():
    with OfflineTerminal() as s:
        s.start()
        s.wait(lambda: "Channel 1: #offline-a" in s.screen.text(), "configured first channel not selected")
        s.open_app_diagnostics()
        assert "Nick: offline_tester" in s.app_line("Nick:")
        s.close_app_diagnostics()
        assert "Users:" not in s.screen.text(), "configured user_list=false was ignored"
        s.navigate("u")
        s.wait(lambda: "Users:" in s.screen.text(), "simulated roster is unavailable")
        s.navigate("c")
        s.wait(lambda: "Channels" in s.panel_title(), "offline channel chooser missing")
        rows = [line for line in s.screen.text().splitlines() if re.search(r"\| \d+: [#&+!]", line)]
        assert len(rows) == 2 and any("#offline-a" in line for line in rows) and any("#offline-b" in line for line in rows)
        s.type(b"\x1b")
        s.wait(lambda: "Channel 1: #offline-a" in s.screen.text(), "channel chooser did not return to the main view")
        s.command("offline local message")
        s.wait(lambda: "offline local message" in "\n".join(s.screen.text().splitlines()[2:-3])
               and "0 bytes" in s.screen.text(), "offline composed message did not pass through local chat rendering")
        s.navigate("m")
        s.wait(lambda: "custom-watch-phrase" in s.screen.text() and "offline_tester" in s.screen.text(),
               "simulated mentions did not exercise nickname and configured custom watch string", timeout=8)
        assert re.search(r"\d\d:\d\d:\d\d", s.screen.text())
        s.navigate("q")
        s.wait(lambda: "Query list" in s.screen.text() and "| Aster" in s.screen.text(),
               "simulated incoming query did not reach the query list", timeout=5)
        s.type(b"\x1b")
        s.wait(lambda: "Query list" not in s.screen.text(), "query chooser did not return to the main view")
        s.command("/save", error="disabled")
        s.wait(lambda: "test" in s.screen.text().lower() and
               ("disabled" in s.screen.text().lower() or "not saved" in s.screen.text().lower()
                or "not written" in s.screen.text().lower()), "offline save did not explain that writing is disabled")
        s.clear_input()  # Refused commands remain editable by design.
        s.command("/disconnect")
        s.open_app_diagnostics()
        assert s.app_line("Connection:") == "Connection: OFFLINE TEST MODE (stopped)"
        s.close_app_diagnostics()
        s.command("/connect")
        s.wait(lambda: "OFFLINE TEST MODE running as offline_tester" in s.screen.text(),
               "offline reconnect failed or switched to a live connection")
        s.command("/channel 1")
        s.wait(lambda: "Channel 1: #offline-a" in s.screen.text(), "reconnect lost configured channel identity")
        assert b"Name lookup failed" not in s.raw and b"TLS handshake failed" not in s.raw
        assert b"Secret must be" not in s.raw and b"SASL credentials could not" not in s.raw
    print("offline: configured nick/channels/roster/watch strings, local chat, query events, reconnect, secret bypass and nonwriting save passed")


def default_simulation():
    with OfflineTerminal(configured=False) as s:
        s.start()
        s.navigate("c")
        s.wait(lambda: "Channels" in s.panel_title(), "default offline channel chooser missing")
        rows = [line for line in s.screen.text().splitlines() if re.search(r"\| \d+: [#&+!]", line)]
        assert len(rows) == 2, "default simulation did not create its two demonstration channels"
        assert "0: server" in s.screen.text()
    print("offline: default demonstration starts without prompts, config, credential access or created files")


def deferred_simulation():
    with OfflineTerminal(configured=False, no_connect=True) as s:
        s.start()
        s.open_app_diagnostics()
        assert "OFFLINE TEST MODE (stopped)" in s.app_line("Connection:")
        s.close_app_diagnostics()
        s.navigate("c")
        s.wait(lambda: "Channels" in s.panel_title(), "stopped simulation channel chooser missing")
        assert "0: server" in s.screen.text()
        assert not any(re.search(r"\| \d+: [#&+!]", line) for line in s.screen.text().splitlines())
        s.type(b"\x1b")
        s.wait(lambda: "Channel 0: Server" in s.screen.text(), "deferred chooser did not return to the server view")
        s.command("/connect")
        s.wait(lambda: "OFFLINE TEST MODE running as" in s.screen.text(),
               "/connect did not start the deferred offline simulator")
    print("offline: --no-connect stays stopped until /connect starts simulation")


def main():
    if not BINARY.is_file(): raise SystemExit("Build ./vipirc first (make).")
    cli_options()
    configured_simulation()
    default_simulation()
    deferred_simulation()
    print("all socket-free offline tests passed")


if __name__ == "__main__":
    main()
