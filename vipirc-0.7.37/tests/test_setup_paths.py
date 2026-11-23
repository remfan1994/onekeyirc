#!/usr/bin/env python3
"""Credential setup paths through each real UI, with only owned local fixtures."""
import argparse
import os
from pathlib import Path
import stat
import tempfile
from unittest.mock import patch

from test_idle_terminal import IdleSession
from test_setup_async import Frontend, FINGERPRINT, SECRET, FAKE_CIPHERTEXT, helper


def reach_destination(ui):
    ui.enter("Choose a panel", "1")
    values = {
        "Use TLS": "yes",
        "Auto-authentication": "sasl",
        "Authentication account": "fixture-account",
        "Authentication password source": "gpg",
        "Authentication GPG file": "create",
    }
    for _ in range(32):
        question = ui.question()
        if question == "Authentication password file":
            ui.wait(question)
            return
        assert "password to encrypt" not in question and question != "Finish"
        ui.enter(question, values.get(question, ""))
    raise AssertionError("credential destination was not reached")


def complete_questions(ui):
    values = {
        "Authentication GPG key fingerprint": FINGERPRINT,
        "Authentication password to encrypt": SECRET,
        "Confirm authentication password": SECRET,
    }
    for _ in range(32):
        question = ui.question()
        if question == "Finish":
            ui.wait(question)
            return
        ui.enter(question, values.get(question, ""))
    raise AssertionError("credential setup did not reach Finish")


def find_on_pages(session, needle, limit=12):
    """Readers and setup summaries retain a visible, scrollable exact path."""
    for _ in range(limit):
        if needle in session.screen.text():
            return
        session.type(b"\x1b[6~")
        session.quiet(.06)
    raise AssertionError("path is not available in the visible reader: %r\n%s" %
                         (needle, session.screen.text()))


def run_case(mode):
    print("credential paths: %s" % mode, flush=True)
    with tempfile.TemporaryDirectory(prefix="irc-setup-paths-") as temporary:
        environment = helper(Path(temporary), "success")
        marker = Path(environment["IRC_SETUP_MARKER"])
        gate = Path(environment["IRC_SETUP_GATE"])
        with patch.dict(os.environ, environment):
            if mode == "gui":
                from test_servers import MultiServerSession
                session = MultiServerSession(cols=132)
            else:
                session = IdleSession(live=True, review=False, full=mode == "idle", cols=120)
        with session as s:
            before = s.config.read_bytes()
            ui = Frontend(s, mode)
            ui.begin()
            raw_start = len(s.raw)
            reach_destination(ui)
            destination = s.directory / "credentials" / "account-password.gpg"
            find_on_pages(s, str(destination))
            assert not destination.parent.exists(), "displaying the default created a directory"
            assert not marker.exists(), "displaying the default started GPG"

            public = s.directory / "public"
            public.mkdir(mode=0o755)
            public.chmod(0o755)
            bad = str(public / "credential.gpg")
            s.type(bad)
            s.wait(lambda: bad in "".join("".join(row) for row in ui.answer_rows()),
                   "custom credential path was not entered")
            s.quiet(.30 if ui.native else .12)
            s.type(b"\r")
            s.wait(lambda: ui.question() == "Authentication password file" and
                   any(term in "".join(s.screen.cells[-3 if ui.native else -1]).lower() for term in ("private", "0700")),
                   "nonprivate destination did not fail visibly before any password question")
            assert not marker.exists() and not Path(bad).exists()
            assert stat.S_IMODE(public.stat().st_mode) == 0o755, "setup changed an existing directory's permissions"
            assert not destination.parent.exists() and s.config.read_bytes() == before
            ui.ping("path-preflight-rejected")

            s.type(b"\x7f" * len(bad))
            s.quiet(.10)
            ui.enter("Authentication password file", "")
            ui.wait("Authentication GPG key fingerprint")
            assert destination.parent.is_dir() and stat.S_IMODE(destination.parent.stat().st_mode) == 0o700
            assert not destination.exists() and not marker.exists(), "path acceptance created ciphertext or ran GPG"
            complete_questions(ui)
            find_on_pages(s, str(destination))
            assert not destination.exists() and not marker.exists()
            assert s.config.read_bytes() == before
            ui.finish()
            s.wait(marker.exists, "Finish did not start the isolated encryption helper")
            ui.ping("path-encryption-pending")
            gate.write_bytes(b"release")
            s.wait(lambda: not ui.active(), "successful credential setup did not close")
            assert destination.read_bytes() == FAKE_CIPHERTEXT
            assert stat.S_IMODE(destination.stat().st_mode) == 0o600
            assert s.config.read_bytes() == before, "Apply without saving wrote configuration"
            assert len(marker.read_text().splitlines()) == 1
            assert SECRET.encode("utf-8") not in s.raw[raw_start:], "password leaked in setup output"

            # A transient success notice is not the only way to recover the
            # created location: the explicit authentication report retains it.
            if ui.native:
                status_start = len(s.raw)
                s.command("/settings", keep_reader=True)
                assert str(destination).encode() not in s.raw[status_start:], "general settings enumerated a credential path"
                assert SECRET.encode("utf-8") not in s.raw[status_start:]
                s.close_reader()
                s.command("/autoauth status", keep_reader=True)
            else:
                s.open_command(from_chat=True)
                s.type("autoauth status")
                s.wait(lambda: s.command_text() == "/autoauth status", "authentication status command was not composed")
                s.type(b"\r")
                s.wait(lambda: "Authentication settings" in s.panel_title(),
                       "authentication status did not open its full-screen reader")
            find_on_pages(s, str(destination))
            assert "SASL GPG file" in s.screen.text()
            assert SECRET.encode("utf-8") not in s.raw[raw_start:]
            ui.ping("path-report-open")
            assert not ui.peer.messages(), "credential setup sent a chat message"
    print("credential paths: %s passed; early refusal, absolute default, private creation, Finish-only encryption, retrievable location and live PING" % mode,
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("one", "idle", "gui", "all"), default="all")
    args = parser.parse_args()
    for mode in (("one", "idle", "gui") if args.mode == "all" else (args.mode,)):
        run_case(mode)


if __name__ == "__main__":
    main()
