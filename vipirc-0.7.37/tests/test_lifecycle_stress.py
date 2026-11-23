#!/usr/bin/env python3
"""Bounded local transport/storage failure and lifecycle stress regressions."""
import argparse
import hashlib
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time

from test_net import cert_pair

ROOT = Path(__file__).resolve().parents[1]


def compile_program(output, sources, sanitize=False, extra=()):
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wshadow", "-Werror"]
    if sanitize:
        flags += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([os.environ.get("CC", "cc"), *flags, "-I", str(ROOT / "src"),
                    *map(str, sources), *extra, "-o", str(output)], check=True, cwd=ROOT)


def run_program(command, timeout):
    began = time.monotonic()
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    assert result.returncode == 0, (command, result.returncode, result.stdout, result.stderr)
    assert not result.stderr, result.stderr
    print(result.stdout.strip(), flush=True)
    return time.monotonic() - began


class Peer:
    def __init__(self, mode, rounds, certificate, key):
        self.mode, self.rounds = mode, rounds
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(4)
        self.listener.settimeout(5)
        self.port = self.listener.getsockname()[1]
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.context.load_cert_chain(certificate, key)
        self.errors = []
        self.completed = 0
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        try:
            for index in range(self.rounds):
                connection, _ = self.listener.accept()
                connection.settimeout(35 if self.mode == "deadline" else 5)
                try:
                    if self.mode in ("stall", "deadline"):
                        assert connection.recv(65536), "TLS peer never sent its ClientHello"
                        while connection.recv(65536):
                            pass
                    else:
                        if self.mode != "bulk-plain":
                            connection = self.context.wrap_socket(connection, server_side=True)
                        if self.mode == "abrupt":
                            connection.sendall(bytes(range(256)))
                            if index % 2:
                                connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                            # SSLSocket.close closes TCP without a TLS close_notify.
                        else:
                            time.sleep(0.2)
                            digest = hashlib.sha256()
                            received = 0
                            while received < 1024 * 1024:
                                block = connection.recv(min(65536, 1024 * 1024 - received))
                                assert block, "sender closed before the full bounded stream"
                                digest.update(block)
                                received += len(block)
                            assert digest.digest() == hashlib.sha256(bytes(range(256)) * 4096).digest()
                            connection.sendall(b"OK")
                    self.completed += 1
                finally:
                    connection.close()
        except Exception as error:
            self.errors.append(error)
        finally:
            self.listener.close()

    def finish(self):
        self.thread.join(timeout=6)
        assert not self.thread.is_alive(), "local peer did not finish"
        assert not self.errors, self.errors
        assert self.completed == self.rounds, (self.completed, self.rounds)


def network_stress(directory, sanitize):
    probe = directory / "net-stress"
    compile_program(probe, [ROOT / "tests/net_stress_probe.c", ROOT / "src/net.c"], sanitize,
                    ["-lssl", "-lcrypto"])
    certificate, key = cert_pair(directory, "localhost", "DNS:localhost,IP:127.0.0.1")
    began = time.monotonic()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as refused:
        refused.bind(("127.0.0.1", 0))  # Reserve a port that is intentionally not listening.
        run_program([probe, "cycles", "127.0.0.1", refused.getsockname()[1], "", 300], 15)
    for mode, rounds in (("stall", 12), ("abrupt", 24), ("bulk-plain", 1), ("bulk-tls", 1), ("deadline", 1)):
        peer = Peer(mode, rounds, certificate, key)
        try:
            run_program([probe, mode, "127.0.0.1", peer.port, certificate, rounds], 40 if mode == "deadline" else 20)
        finally:
            peer.finish()
    print("network stress: 339 transport lifetimes; 2 MiB verified under backpressure; elapsed=%.3fs; sanitizer=%s" %
          (time.monotonic() - began, sanitize), flush=True)


def storage_stress(directory, sanitize):
    config_object = directory / "config-faults.o"
    compile_program(config_object, [ROOT / "src/config.c"], sanitize,
                    ["-c", "-Dfsync=storage_test_fsync", "-Drenameat=storage_test_renameat"])
    probe = directory / "storage-stress"
    compile_program(probe, [ROOT / "tests/test_storage_stress.c", config_object,
                    ROOT / "src/protocol.c", ROOT / "src/app.c", ROOT / "src/autocommands.c",
                    ROOT / "src/sim.c", ROOT / "src/net.c",
                    ROOT / "src/credentials.c"],
                    sanitize, ["-lssl", "-lcrypto"])
    run_program([probe], 25)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--network-only", action="store_true")
    group.add_argument("--storage-only", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="vipirc-lifecycle-stress-") as name:
        directory = Path(name)
        if not args.storage_only:
            network_stress(directory, args.sanitize)
        if not args.network_only:
            storage_stress(directory, args.sanitize)


if __name__ == "__main__":
    main()
