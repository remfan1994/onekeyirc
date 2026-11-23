#!/usr/bin/env python3
"""Exercise resolver ownership without DNS requests or unrelated processes."""
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

    def run(command, label):
        started = time.monotonic()
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, start_new_session=True)
        try:
            stdout, stderr = process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
            stderr += "\nFixture process group killed after timeout.\n"
        commands.append({"label": label, "command": command, "returncode": process.returncode,
                         "elapsed_seconds": time.monotonic() - started, "stdout": stdout, "stderr": stderr})
        print(f"{label}: {'PASS' if process.returncode == 0 else 'FAIL'}", flush=True)
        if stdout:
            print(stdout, end="", flush=True)
        if stderr:
            print(stderr, end="", flush=True)
        return process.returncode == 0

    okay = False
    try:
        with tempfile.TemporaryDirectory(prefix="irc-resolver-ownership-") as temporary:
            directory = Path(temporary)
            obj, binary = directory / "net.o", directory / "resolver-ownership"
            compiler = shlex.split(os.environ.get("CC", "cc"))
            flags = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
            flags += shlex.split(os.environ.get("CPPFLAGS", "")) + shlex.split(os.environ.get("CFLAGS", ""))
            include = ["-I", str(root / "src")]
            renames = [f"-D{name}=resolver_ownership_{name}" for name in ("fork", "pipe", "kill", "getaddrinfo")]
            if not run(compiler + flags + include + renames + ["-c", str(root / "src/net.c"), "-o", str(obj)], "compile resolver transport"):
                return 1
            if not run(compiler + flags + include + [str(fixture), str(obj)] +
                       shlex.split(os.environ.get("LDFLAGS", "")) + ["-lssl", "-lcrypto", "-o", str(binary)], "compile resolver fixture"):
                return 1
            report["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
            okay = run([str(binary)], "resolver child ownership")
    finally:
        report["passed"] = okay
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return 0 if okay else 1


if __name__ == "__main__":
    raise SystemExit(main())
