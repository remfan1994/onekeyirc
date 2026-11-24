#!/usr/bin/env python3
"""Noninteractive CLI/config boundaries; never contact a network or real account."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("ONEKEY_BINARY", ROOT / "onekeyirc")).resolve()
FULL_SETTING = os.environ.get("VIPIRC_IDLE_BINARY")
FULL = None if FULL_SETTING == "" else Path(FULL_SETTING or ROOT / "vipirc").resolve()


def run(binary, *args, env):
    return subprocess.run([str(binary), *map(str, args)], input=b"", capture_output=True,
                          env=env, cwd=ROOT, timeout=10)


def main():
    with tempfile.TemporaryDirectory(prefix="onekey-cli-") as temp:
        directory = Path(temp)
        env = dict(os.environ, XDG_CONFIG_HOME=str(directory), LC_ALL="C", TERM="xterm")
        missing = directory / "missing.ini"
        information_env = dict(env, XDG_CONFIG_HOME="not-an-absolute-directory")
        results = {}
        for option in ("-h", "--help", "-v", "-V", "--version", "--manual"):
            result = run(BINARY, "--config", missing, option, env=information_env)
            assert result.returncode == 0 and result.stderr == b"", (option, result)
            assert b"ThatIAm.vip" in result.stdout and b"remfan1994" in result.stdout
            assert b"IALA" in result.stdout
            assert b"vipirc" not in result.stdout.lower(),("standalone information names another product",option,result.stdout)
            results[option] = result.stdout
        assert results["-h"] == results["--help"]
        assert results["-v"] == results["-V"] == results["--version"]
        expected = re.search(rb'^#define ONEKEY_VERSION "([^"]+)"$', (ROOT / "src/app.h").read_bytes(), re.M)
        assert expected and re.match(rb"^onekeyirc " + re.escape(expected[1]) + rb"(?: \[lang-rtl-r[0-9]+\])?\n", results["-v"])
        assert b"/help ls" in results["-h"] and b"/help COMMAND" in results["-h"] and b"/autoconnect" in results["-h"]
        assert b"/quickstart" in results["-h"] and b"/panel" in results["-h"]
        assert b"send_mode" in results["--manual"]
        assert not any(directory.iterdir()), "informational options wrote files"

        result = run(BINARY, "--init-config", env=env)
        assert result.returncode == 0, result
        native = directory / "onekeyirc/config.ini"
        assert native.is_file() and native.stat().st_mode & 0o777 == 0o600
        saved = native.read_bytes()
        assert b"send_mode=direct" in saved and b"[output]\nformat=compact\n" in saved
        assert b"[interface]" not in saved and b"mouse=" not in saved and b"user_list=" not in saved and b"hints=" not in saved and b"shortcut_labels=" not in saved
        result = run(BINARY, "--check-config", env=env)
        assert result.returncode == 0 and b"valid" in result.stdout, result
        result = run(BINARY, "--init-config", env=env)
        assert result.returncode == 1 and native.read_bytes() == saved
        for value in (b"true", b"REVIEW", b"invalid", b"review extra"):
            native.write_bytes(saved.replace(b"send_mode=direct", b"send_mode=" + value))
            result = run(BINARY, "--check-config", env=env)
            assert result.returncode == 2 and b"line" in result.stderr, result
        for value in (b"verbose", b"FULL", b"1", b"full extra"):
            native.write_bytes(saved.replace(b"format=compact", b"format=" + value))
            result = run(BINARY, "--check-config", env=env)
            assert result.returncode == 2 and b"line" in result.stderr, result
        native.write_bytes(saved.replace(b"format=compact", b"format=full"))
        assert run(BINARY, "--check-config", env=env).returncode == 0
        native.write_bytes(saved.replace(b"send_mode=direct", b"send_mode=review"))
        assert run(BINARY, "--check-config", env=env).returncode == 0

        if FULL is not None and FULL.exists():
            full_source_setting = os.environ.get("VIPIRC_SOURCE_ROOT")
            full_source = Path(full_source_setting or ROOT).resolve()
            full_version = re.search(rb'^#define VIPIRC_VERSION "([^"]+)"$',
                                     (full_source / "src/app.h").read_bytes(), re.M)
            assert full_version, "vipirc version definition missing"
            full_manual_source = full_source / "doc/manual.txt"
            for prefix in ([],["-i"]):
                for option in ("-h","--help","-v","--manual"):
                    information=run(FULL,*prefix,"--config",missing,option,env=information_env)
                    assert information.returncode==0 and not information.stderr,(prefix,option,information)
                    if not prefix and option == "--manual":
                        # The complete manual intentionally documents shared
                        # onekeyirc session controls and upgrade instructions.
                        title = b"vipirc " + full_version[1] + b" - terminal IRC client"
                        assert information.stdout.splitlines()[:1] == [title], "full manual has the wrong product/version title"
                        if full_source_setting or full_manual_source.is_file():
                            assert information.stdout == full_manual_source.read_bytes(), "full manual differs from its package source"
                    else:
                        assert b"vipirc" in information.stdout.lower() and b"onekeyirc" not in information.stdout.lower(), \
                            ("vipirc information names another product",prefix,option,information.stdout)
                    if option == "-v":
                        assert information.stdout.startswith(b"vipirc " + full_version[1] + b"\n"), "vipirc version differs from its package source"
            result = run(FULL, "--init-config", env=env)
            assert result.returncode == 0, result
            full_config = directory / "vipirc/config.ini"
            full_saved = full_config.read_bytes()
            assert b"[interface]" in full_saved and b"send_mode=direct" in full_saved
            assert b"hints=true\n" in full_saved and b"shortcut_labels=true\n" in full_saved and b"[output]\nformat=compact\n" in full_saved
            assert run(FULL, "-i", "--check-config", env=env).returncode == 0
            a = run(FULL, "-i", "-h", env=env)
            b = run(FULL, "-h", "-i", env=env)
            assert a.returncode == b.returncode == 0 and a.stdout == b.stdout
            assert b"Usage: vipirc -i" in a.stdout and b"/help ls" in a.stdout
            result = run(BINARY, "--config", full_config, "--check-config", env=env)
            assert result.returncode == 2 and b"[interface]" in result.stderr
            assert full_config.read_bytes() == full_saved
            assert run(FULL, "--config", native, "--check-config", env=env).returncode == 0
        result = run(BINARY, "--test-mode", env=env)
        assert result.returncode == 2 and b"requires a terminal" in result.stderr
        for args in (("--idle", "unexpected"), ("--unknown",), ("--port", "0", "--check-config")):
            assert run(BINARY, *args, env=env).returncode == 2

    if shutil.which("readelf"):
        dynamic = subprocess.check_output(["readelf", "-d", str(BINARY)], text=True)
        assert not re.search(r"Shared library:.*(?:curses|tinfo)", dynamic, re.I), dynamic
    print("idle CLI: help/version/manual, independent native config, full-config preservation, strict mode validation, nonterminal errors and curses-free standalone linkage passed")
    print("binary SHA256:", hashlib.sha256(BINARY.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
