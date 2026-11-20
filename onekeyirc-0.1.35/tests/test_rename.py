#!/usr/bin/env python3
"""Rename migration through the public CLI; isolated files and no IRC access."""
import argparse
import os
from pathlib import Path
import stat
import subprocess
import tempfile


def run(binary, env, *arguments, expected=0):
    result = subprocess.run(
        [str(binary), *map(str, arguments)], input=b"", capture_output=True,
        env=env, timeout=10,
    )
    assert result.returncode == expected, (
        tuple(map(str, arguments)), result.returncode, result.stdout, result.stderr,
    )
    return result


def private_file(path, contents):
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.write_bytes(contents)
    path.chmod(0o600)


def snapshot(directory):
    """Record objects without following symlinks or reading any outside file."""
    result = {}
    for path in sorted(directory.rglob("*")):
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            contents = os.readlink(path)
        elif stat.S_ISREG(info.st_mode):
            contents = path.read_bytes()
        else:
            contents = None
        result[str(path.relative_to(directory))] = (
            info.st_mode, info.st_uid, info.st_gid, contents,
        )
    return result


def environment(directory, *, xdg=True):
    home = directory / "home"
    home.mkdir(mode=0o700)
    env = dict(os.environ, HOME=str(home), LC_ALL="C", TERM="xterm")
    if xdg:
        base = directory / "config"
        base.mkdir(mode=0o700)
        env["XDG_CONFIG_HOME"] = str(base)
    else:
        base = home / ".config"
        env.pop("XDG_CONFIG_HOME", None)
    return env, base


VALID = b"[connection]\nserver=primary.invalid\nnick=rename_fixture\n"
INVALID = b"[input]\nsend_mode=invalid-rename-fixture\n"


def defaults_and_fallback(onekey):
    for setting in ("absolute-xdg", "unset-xdg", "empty-xdg"):
        with tempfile.TemporaryDirectory(prefix="onekeyirc-rename-") as name:
            directory = Path(name)
            env, base = environment(directory, xdg=setting == "absolute-xdg")
            if setting == "empty-xdg":
                env["XDG_CONFIG_HOME"] = ""
            new = base / "onekeyirc" / "config.ini"
            old = base / "1keyirc" / "config.ini"

            # Merely checking absent defaults must not create directories.
            before = snapshot(directory)
            run(onekey, env, "--check-config")
            assert snapshot(directory) == before

            # A new installation uses the new name, including its private files.
            created = run(onekey, env, "--init-config")
            assert str(new).encode() in created.stdout, created.stdout
            assert new.is_file() and not old.exists()
            assert stat.S_IMODE(new.stat().st_mode) == 0o600
            assert stat.S_IMODE(new.parent.stat().st_mode) == 0o700
            assert new.read_bytes().startswith(b"# onekeyirc configuration.")
            new.unlink()
            new.parent.rmdir()

            # An old installation stays at its original path. Its profile
            # catalog is resolved relative to that path, not the new default.
            private_file(old, VALID)
            catalog = Path(str(old) + ".servers")
            secondary = Path(str(old) + ".d") / "secondary"
            private_file(catalog, b"[servers]\nprofile=secondary\ndefault=secondary\n")
            private_file(secondary, b"[connection]\nserver=secondary.invalid\nnick=secondary\n")
            private_file(Path(str(old) + ".commands"), b"fixture command counts remain in place\n")
            private_file(old.parent / "credentials" / "account-password.gpg",
                         b"fixture ciphertext marker, not an actual credential\n")
            before = snapshot(directory)
            run(onekey, env, "--check-config")
            refusal = run(onekey, env, "--init-config", expected=1)
            assert b"already exists" in refusal.stderr
            assert snapshot(directory) == before and not new.exists()

            secondary.unlink()
            before = snapshot(directory)
            rejected = run(onekey, env, "--check-config", expected=2)
            assert b"secondary" in rejected.stderr and b"missing" in rejected.stderr
            assert snapshot(directory) == before

            # A valid new default wins even if the old profile set is invalid.
            private_file(new, VALID)
            before = snapshot(directory)
            run(onekey, env, "--check-config")
            assert snapshot(directory) == before

            # A present but invalid new file is not silently replaced by old
            # settings; an explicit valid file still takes precedence.
            private_file(secondary, b"[connection]\nserver=secondary.invalid\nnick=secondary\n")
            run(onekey, env, "--config", old, "--check-config")
            private_file(new, INVALID)
            explicit = directory / "explicit" / "config.ini"
            private_file(explicit, VALID)
            before = snapshot(directory)
            run(onekey, env, "--check-config", expected=2)
            run(onekey, env, "--config", explicit, "--check-config")
            assert snapshot(directory) == before

    print("rename: new defaults, HOME/XDG selection, legacy profile retention, "
          "new-name precedence and explicit config authority passed", flush=True)


def unsafe_candidates(onekey):
    for case in ("new-leaf", "new-broken-leaf", "new-parent", "old-leaf", "old-parent",
                 "new-parent-file", "new-unsafe-mode", "old-unsafe-mode"):
        with tempfile.TemporaryDirectory(prefix="onekeyirc-rename-unsafe-") as name:
            directory = Path(name)
            env, base = environment(directory)
            new = base / "onekeyirc" / "config.ini"
            old = base / "1keyirc" / "config.ini"
            explicit = directory / "explicit" / "config.ini"
            private_file(explicit, VALID)
            if case.startswith("new-"):
                private_file(old, VALID)
            if case.endswith("unsafe-mode"):
                candidate = new if case.startswith("new-") else old
                private_file(candidate, VALID)
                candidate.chmod(0o666)
            elif case.endswith("leaf"):
                candidate = new if case.startswith("new-") else old
                candidate.parent.mkdir(mode=0o700, exist_ok=True)
                target = directory / "missing.ini" if "broken" in case else explicit
                candidate.symlink_to(target)
            elif case.endswith("parent-file"):
                private_file(new.parent, b"not a directory\n")
            else:
                candidate = new if case.startswith("new-") else old
                candidate.parent.symlink_to(explicit.parent, target_is_directory=True)
            before = snapshot(directory)
            run(onekey, env, "--check-config", expected=2)
            run(onekey, env, "--config", explicit, "--check-config")
            assert snapshot(directory) == before

    with tempfile.TemporaryDirectory(prefix="onekeyirc-rename-relative-") as name:
        directory = Path(name)
        env, unused = environment(directory)
        explicit = directory / "explicit.ini"
        private_file(explicit, VALID)
        env["XDG_CONFIG_HOME"] = "not-an-absolute-directory"
        before = snapshot(directory)
        run(onekey, env, "--check-config", expected=2)
        run(onekey, env, "--config", explicit, "--check-config")
        assert snapshot(directory) == before
    print("rename: unsafe default candidates fail closed without writes; "
          "explicit config bypasses default discovery", flush=True)


def branding_and_parity(onekey, vipirc):
    with tempfile.TemporaryDirectory(prefix="onekeyirc-rename-brand-") as name:
        directory = Path(name)
        env, base = environment(directory)
        invalid_env = dict(env, XDG_CONFIG_HOME="relative-invalid")
        before = snapshot(directory)
        version = run(onekey, invalid_env, "--version")
        assert version.stdout.splitlines()[0].startswith(b"onekeyirc "), version.stdout
        help_text = run(onekey, invalid_env, "--help")
        assert help_text.stdout.startswith(b"Usage: onekeyirc "), help_text.stdout
        error = run(onekey, env, "unexpected-operand", expected=2)
        assert error.stderr.startswith(b"onekeyirc: "), error.stderr
        assert snapshot(directory) == before

        # Both vipirc interfaces continue to select vipirc/config.ini even
        # when both standalone names contain invalid configurations.
        if vipirc is not None:
            private_file(base / "onekeyirc" / "config.ini", INVALID)
            private_file(base / "1keyirc" / "config.ini", INVALID)
            run(vipirc, env, "--init-config")
            full = base / "vipirc" / "config.ini"
            assert b"[interface]" in full.read_bytes()
            before = snapshot(directory)
            for prefix in ((), ("-i",)):
                run(vipirc, env, *prefix, "--check-config")
                result = run(vipirc, invalid_env, *prefix, "--version")
                assert result.stdout.splitlines()[0].startswith(b"vipirc ")
            assert snapshot(directory) == before
    print("rename: full standalone branding and unchanged vipirc/full and -i "
          "default configuration behavior passed" if vipirc is not None else
          "rename: complete standalone CLI branding passed", flush=True)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--onekey", type=Path,
                        default=Path(os.environ.get("ONEKEY_BINARY", root / "onekeyirc")))
    parser.add_argument("--vipirc", type=Path)
    args = parser.parse_args()
    onekey = args.onekey.resolve(strict=True)
    vipirc = args.vipirc.resolve(strict=True) if args.vipirc else None
    defaults_and_fallback(onekey)
    unsafe_candidates(onekey)
    branding_and_parity(onekey, vipirc)
    print("all rename migration CLI checks passed", flush=True)


if __name__ == "__main__":
    main()
