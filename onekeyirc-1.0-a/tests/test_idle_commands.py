#!/usr/bin/env python3
"""Nonterminal command/manual and native config contracts; no IRC connection."""
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ONEKEY = Path(os.environ.get("ONEKEY_BINARY", ROOT / "onekeyirc")).resolve()
FULL = os.environ.get("VIPIRC_IDLE_BINARY")


def main():
    assert ONEKEY.is_file(), "Build onekeyirc or set ONEKEY_BINARY first."
    with tempfile.TemporaryDirectory(prefix="onekeyirc-command-cli-") as directory:
        private = Path(directory)
        env = dict(os.environ, HOME=str(private), XDG_CONFIG_HOME=str(private / "xdg"))

        def run(binary, args, expected=0):
            result = subprocess.run([str(binary), *args], input=b"", stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, env=env, cwd=ROOT, timeout=10)
            assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
            return result

        for option in ("-h", "--help"):
            text = run(ONEKEY, [option]).stdout.decode()
            assert "vipirc" not in text.lower(),"standalone help names another product"
            for wanted in ("Usage: onekeyirc", "-h, --help", "-v, --version", "/help ls",
                           "/help COMMAND", "/connect irc.example.org", "/join #channel", "/saveconf", "/quickstart", "/panel", "ThatIAm.vip", "remfan1994", "IALA"):
                assert wanted in text, (option, wanted, text)
        versions = [run(ONEKEY, [option]).stdout for option in ("-v", "-V", "--version")]
        expected = re.search(rb'^#define ONEKEY_VERSION "([^"]+)"$',
                             (ROOT / "src/app.h").read_bytes(), re.M)
        assert expected, "missing application version"
        version = expected[1]
        marker = b" [lang-rtl-" + version + b"]" if (ROOT / "LANGUAGE-PACK-REVISION.txt").is_file() else b""
        assert len(set(versions)) == 1 and versions[0].splitlines()[0] == b"onekeyirc " + version + marker
        manual = run(ONEKEY, ["--manual"]).stdout.decode()
        expected_manual = (ROOT / "doc/onekeyirc-manual.txt").read_text()
        assert manual == expected_manual, "Standalone manual differs from its documented complete source."
        assert "vipirc" not in manual.lower(),"standalone manual names another product"
        assert "vipirc" not in (ROOT/"doc/onekeyirc.1").read_text().lower(),"standalone man page names another product"
        for wanted in ("Read help topics: /help ls.", "The command system", "autoauth -",
                       "Usage: /ls", "Usage: /connect [-tls|-plain] [-nick NICK] HOST [PORT]", "Usage: /saveconf", "Usage: /query",
                       "Usage: /quickstart", "Usage: /panel", "Enter with input focus runs the edited command",
                       "Enter inserts the selected command for editing", "separate Enter after display executes it",
                       "Ctrl-D is inert", "Escape on the log backgrounds the client"):
            assert wanted in manual, wanted
        assert run(ONEKEY, ["-i", "--manual"]).stdout.decode() == manual
        assert run(ONEKEY, ["--manual", "--idle"]).stdout.decode() == manual
        run(ONEKEY, ["--not-an-option"], expected=2)

        config = private / "config.ini"
        run(ONEKEY, ["--config", str(config), "--init-config"])
        generated = config.read_bytes()
        assert stat.S_IMODE(config.stat().st_mode) == 0o600
        assert b"[input]\nsend_mode=direct\n" in generated and b"[output]\nformat=compact\n" in generated
        assert b"[history]\nlines=100\n" in generated and b"[interface]" not in generated and b"hints=" not in generated and b"shortcut_labels=" not in generated
        run(ONEKEY, ["--config", str(config), "--init-config"], expected=1)
        assert config.read_bytes() == generated, "--init-config overwrote an existing profile"
        run(ONEKEY, ["--config", str(config), "--check-config"])

        # Native program rejects presentation controls explicitly rather than
        # silently adopting another application's interface or rewriting it.
        incompatible = private / "full-interface.ini"
        incompatible.write_bytes(generated + b"\n[interface]\nmouse=true\n")
        incompatible.chmod(0o600)
        result = run(ONEKEY, ["--config", str(incompatible), "--check-config"], expected=2)
        assert b"[interface]" in result.stderr

        # Informational operations must neither open credentials nor connect.
        secret = private / "never-open-credential"
        os.mkfifo(secret, 0o600)
        secret_config = generated.replace(b"password_file=\n", ("password_file=" + str(secret) + "\n").encode(), 1)
        config.write_bytes(secret_config)
        run(ONEKEY, ["--config", str(config), "--check-config"])
        run(ONEKEY, ["--config", str(config), "--manual"])
        assert config.read_bytes() == secret_config

        if FULL:
            full = Path(FULL).resolve()
            full_source = Path(os.environ.get("VIPIRC_SOURCE_ROOT") or ROOT).resolve()
            expected_full = (full_source/"doc/vipirc-idle-manual.txt").read_text()
            assert "vipirc" in expected_full.lower() and "onekeyirc" not in expected_full.lower()
            # The generated full man page shares the full manual's explicit
            # companion-client comparisons. Its product header, not every word
            # in its body, determines which program the page documents.
            man_header = next(line for line in (full_source/"doc/vipirc.1").read_text().splitlines()
                              if line.startswith(".TH "))
            assert man_header.startswith('.TH VIPIRC 1 '), "full man page has the wrong product header"
            for args in (["-i", "--manual"], ["--manual", "--idle"]):
                assert run(full, args).stdout.decode() == expected_full, args
            full_manual=run(full,["--manual"]).stdout
            assert full_manual==(full_source/"doc/manual.txt").read_bytes(),"curses manual differs from embedded documentation"
            full_version = re.search(rb'^#define VIPIRC_VERSION "([^"]+)"$',
                                     (full_source/"src/app.h").read_bytes(), re.M)
            assert full_version, "vipirc version definition missing"
            assert '"vipirc ' + full_version[1].decode() + '"' in man_header, "full man page has the wrong release version"
            # Shared session and upgrade guidance may name the companion;
            # the complete manual must retain its exact vipirc release title.
            title = b"vipirc " + full_version[1] + b" - terminal IRC client"
            assert full_manual.splitlines()[:1] == [title], "full manual has the wrong product/version title"
            run(full, ["-i", "--config", str(config), "--check-config"])
            print("paired compatibility: onekeyirc, vipirc -i and vipirc manuals match their package sources; native config loads in vipirc -i")
    print("idle CLI: standard help/version, exact complete topic manual, independent native config, no overwrite, explicit interface rejection, credential-free informational commands and optional vipirc-idle compatibility passed")


if __name__ == "__main__":
    main()
