#!/usr/bin/env python3
"""Verified loopback TLS fairness, record buffering, and write retry ordering.

Only the want-read case injects a TLS return value. Certificates and private
keys are generated inside an owner-only temporary directory. No external
network service, user configuration, or running client is accessed.
"""
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


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--report", type=Path)
    parser.add_argument("--updates", type=int, default=10000)
    args = parser.parse_args()
    root = args.source_root.resolve()
    fixture = Path(__file__).with_suffix(".c").resolve()
    records = []
    report = {"source_root": str(root), "fixture": str(fixture), "commands": records,
              "inputs": {str(p): digest(p) for p in (root / "src/net.c", root / "src/net.h", fixture)},
              "environment": {k: os.environ[k] for k in ("CC", "CFLAGS", "CPPFLAGS", "LDFLAGS", "ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}}

    def run(command, label, timeout=35):
        started = time.monotonic()
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, start_new_session=True)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
            stderr += "\nFixture process group killed after timeout.\n"
        result = {"label": label, "command": command, "returncode": process.returncode,
                  "elapsed_seconds": time.monotonic() - started,
                  "stdout": stdout, "stderr": stderr}
        records.append(result)
        print(f"{label}: {'PASS' if process.returncode == 0 else 'FAIL'}", flush=True)
        if stdout:
            print(stdout, end="", flush=True)
        if stderr and label != "generate private loopback certificate":
            print(stderr, end="", flush=True)
        return process.returncode == 0

    success = True
    try:
        with tempfile.TemporaryDirectory(prefix="irc-tls-budget-") as temporary:
            directory = Path(temporary)
            compiler = shlex.split(os.environ.get("CC", "cc"))
            flags = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
            flags += shlex.split(os.environ.get("CPPFLAGS", ""))
            flags += shlex.split(os.environ.get("CFLAGS", ""))
            linker = shlex.split(os.environ.get("LDFLAGS", ""))
            capability = directory / "capability.c"
            capability.write_text(
                "#include <openssl/ssl.h>\n"
                "int main(int argc, char **argv) { (void)argv; if (argc == 12345) {\n"
                "SSL_key_update((SSL *)0, SSL_KEY_UPDATE_NOT_REQUESTED);\n"
                "SSL_set0_wbio((SSL *)0, (BIO *)0);\n"
                "SSL_CTX_set_num_tickets((SSL_CTX *)0, 0);\n"
                "SSL_has_pending((SSL *)0);\n"
                "return SSL3_MT_KEY_UPDATE; } return 0; }\n", encoding="ascii")
            if not run(compiler + flags + [str(capability)] + linker +
                       ["-lssl", "-lcrypto", "-o", str(directory / "capability")],
                       "check TLS fixture API availability"):
                report["available"] = False
                success = False
                print("UNAVAILABLE: this fixture needs OpenSSL KeyUpdate and memory-BIO APIs; no TLS execution is claimed.", flush=True)
                return 77
            report["available"] = True
            cert, key = directory / "cert.pem", directory / "key.pem"
            if not run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-keyout", str(key), "-out", str(cert), "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"],
                       "generate private loopback certificate"):
                return 1
            report["certificate_sha256"] = digest(cert)
            include = ["-I", str(root / "src")]
            renames = [f"-DSSL_{name}=tls_budget_SSL_{name}" for name in ("new", "read", "write", "get_error")]
            obj, binary = directory / "net.o", directory / "tls-budget"
            if not run(compiler + flags + include + renames + ["-c", str(root / "src/net.c"), "-o", str(obj)], "compile production transport"):
                return 1
            if not run(compiler + flags + include + [str(fixture), str(obj)] +
                       linker + ["-lssl", "-lcrypto", "-o", str(binary)], "compile TLS fixture"):
                return 1
            report["binary_sha256"] = digest(binary)
            for case in ("budget", "coalesced", "fragment", "fragment12", "truncated", "corrupt",
                         "write-priority", "write-priority12", "want-read", "reset-write"):
                if not run([str(binary), str(cert), str(key), case, str(args.updates)], case):
                    success = False
    finally:
        report["passed"] = success and all(record["returncode"] == 0 for record in records)
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
