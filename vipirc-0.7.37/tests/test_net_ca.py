#!/usr/bin/env python3
"""Local CA loading boundaries, pinned-file reads, and PEM trust preservation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve()
    fixture = Path(__file__).with_suffix(".c").resolve()
    commands = []
    report = {"source_root": str(root), "commands": commands,
              "inputs": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (root / "src/net.c", root / "src/net.h", fixture)},
              "environment": {k: os.environ[k] for k in ("CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}}

    def run(command, label, timeout=15):
        started = time.monotonic()
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, start_new_session=True)
        timed_out = False
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
        commands.append({"label": label, "command": command, "returncode": process.returncode,
                         "elapsed_seconds": time.monotonic() - started, "timed_out": timed_out,
                         "stdout": stdout, "stderr": stderr})
        okay = not timed_out and process.returncode == 0
        print(f"{label}: {'PASS' if okay else 'FAIL'}", flush=True)
        if not okay:
            print(stdout + stderr, flush=True)
            raise AssertionError(label)

    okay = False
    try:
        with tempfile.TemporaryDirectory(prefix="irc-ca-files-") as temporary:
            directory = Path(temporary)
            binary = directory / "ca-files"
            compiler = shlex.split(os.environ.get("CC", "cc"))
            flags = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
            flags += shlex.split(os.environ.get("CPPFLAGS", "")) + shlex.split(os.environ.get("CFLAGS", ""))
            run(compiler + flags + ["-I", str(root / "src"), str(fixture)] +
                shlex.split(os.environ.get("LDFLAGS", "")) + ["-lssl", "-lcrypto", "-o", str(binary)],
                "compile CA fixture", timeout=60)
            report["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
            key, cert, trusted, crl = (directory / name for name in ("key.pem", "cert.pem", "trusted.pem", "crl.pem"))
            run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-days", "2",
                 "-subj", "/CN=localhost", "-keyout", str(key), "-out", str(cert)], "generate temporary CA")
            run(["openssl", "x509", "-in", str(cert), "-trustout", "-addtrust", "serverAuth",
                 "-addreject", "clientAuth", "-out", str(trusted)], "generate auxiliary trust fixture")
            (directory / "index").write_text("", encoding="ascii")
            (directory / "crlnumber").write_text("01\n", encoding="ascii")
            ca_config = directory / "ca.conf"
            ca_config.write_text("[ca]\ndefault_ca=local\n[local]\n" +
                                 f"database={directory / 'index'}\nprivate_key={key}\ncertificate={cert}\n" +
                                 f"crlnumber={directory / 'crlnumber'}\ndefault_md=sha256\ndefault_crl_days=1\n",
                                 encoding="ascii")
            run(["openssl", "ca", "-batch", "-gencrl", "-config", str(ca_config), "-out", str(crl)],
                "generate temporary CRL")
            encrypted_key = directory / "encrypted-key.pem"
            run(["openssl", "rsa", "-in", str(key), "-aes256", "-passout", "pass:fixture-only",
                 "-out", str(encrypted_key)], "generate encrypted-key compatibility fixture")

            def check(label, path, expected=0, certificates=0, crls=0, auxiliary=0, mode="normal", replacement=None):
                command = [str(binary), str(path), str(expected), str(certificates), str(crls), str(auxiliary), mode]
                if replacement is not None:
                    command.append(str(replacement))
                run(command, label, timeout=3)

            check("valid certificate", cert, 1, 1)
            check("auxiliary trust and rejection", trusted, 1, 1, auxiliary=1)
            check("CRL-only bundle compatibility", crl, 1, 0, 1)
            mixed = directory / "mixed.pem"
            mixed.write_bytes(b"# local CA bundle\n\n" + cert.read_bytes() + crl.read_bytes())
            check("certificate plus CRL", mixed, 1, 1, 1)
            duplicates = directory / "duplicates.pem"
            duplicates.write_bytes(mixed.read_bytes() * 3)
            check("duplicate certificates and CRLs", duplicates, 1, 1, 1)
            for name, private_key in (("plain-key", key), ("encrypted-key", encrypted_key)):
                with_key = directory / f"bundle-{name}.pem"
                with_key.write_bytes(cert.read_bytes() + private_key.read_bytes())
                check(f"certificate with unused {name} remains noninteractive", with_key, 1, 1)
            linked = directory / "symlink.pem"
            linked.symlink_to(cert)
            check("symlink to regular CA", linked, 1, 1)
            check("interrupted read retries", cert, 1, 1, mode="eintr")

            swapped, replacement = directory / "swapped.pem", directory / "replacement-fifo"
            swapped.write_bytes(cert.read_bytes())
            os.mkfifo(replacement, 0o600)
            check("opened CA remains pinned after path replacement", swapped, 1, 1, mode="swap", replacement=replacement)
            for mode in ("shrink", "grow"):
                changing = directory / f"{mode}.pem"
                changing.write_bytes(cert.read_bytes())
                check(f"file {mode} during read rejects", changing, mode=mode)

            fifo = directory / "fifo"
            os.mkfifo(fifo, 0o600)
            check("FIFO without writer rejects promptly", fifo)
            check("directory rejects", directory)
            check("device rejects", Path("/dev/zero"))
            check("missing file rejects", directory / "absent")
            empty = directory / "empty"
            empty.touch()
            check("empty file rejects", empty)
            garbage = directory / "garbage"
            garbage.write_text("This is not a certificate or a CRL.\n", encoding="ascii")
            check("unrecognized bundle rejects", garbage)
            malformed = directory / "malformed.pem"
            malformed.write_bytes(cert.read_bytes() + b"-----BEGIN CERTIFICATE-----\ninvalid\n-----END CERTIFICATE-----\n")
            check("malformed trailing certificate rejects whole bundle", malformed)
            encrypted = directory / "encrypted.pem"
            encrypted.write_text("-----BEGIN CERTIFICATE-----\nProc-Type: 4,ENCRYPTED\n" +
                                 "DEK-Info: AES-256-CBC,00000000000000000000000000000000\n\n" +
                                 "AAAAAAAAAAAAAAAAAAAAAA==\n-----END CERTIFICATE-----\n", encoding="ascii")
            check("encrypted certificate rejects without prompting", encrypted)

            limit = 16 * 1024 * 1024
            maximum = directory / "maximum.pem"
            payload = cert.read_bytes()
            # Comment text is legal in a PEM bundle; exactly the limit remains usable.
            maximum.write_bytes(payload + b"#" + b"x" * (limit - len(payload) - 2) + b"\n")
            check("exact 16 MiB regular bundle", maximum, 1, 1)
            oversized = directory / "oversized.pem"
            with oversized.open("wb") as stream:
                stream.truncate(limit + 1)
            check("over 16 MiB rejects before allocation", oversized)
            okay = True
    finally:
        report["passed"] = okay
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return 0 if okay else 1


if __name__ == "__main__":
    raise SystemExit(main())
