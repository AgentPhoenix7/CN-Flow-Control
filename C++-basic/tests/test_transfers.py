#!/usr/bin/env python3
"""End-to-end CRC-32 transfer checks using the real sender and receiver."""
from __future__ import annotations

import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

PROJECT = Path(__file__).resolve().parents[1]
SCRATCH = PROJECT.parent / ".scratch" / "tmp"


def main() -> None:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(SCRATCH), TMP=str(SCRATCH), TEMP=str(SCRATCH))
    cases = [("empty", b""), ("one", b"\x00"), ("full", bytes(range(46))),
             ("partial", bytes(range(47))),
             ("wrap", bytes(i % 256 for i in range(46 * 257 + 1)))]
    count = 0
    with tempfile.TemporaryDirectory(prefix="crc32-transfers-", dir=SCRATCH) as directory:
        work = Path(directory)
        for protocol in ("stop-and-wait", "go-back-n", "selective-repeat"):
            for name, data in [*cases, ("impaired", bytes(range(256)) * 2)]:
                with socket.socket() as reservation:
                    reservation.bind(("127.0.0.1", 0))
                    port = str(reservation.getsockname()[1])
                source, output = work / "input.bin", work / "output.bin"
                source.write_bytes(data)
                common = ["--protocol", protocol, "--port", port]
                if protocol != "stop-and-wait":
                    common += ["--window", "8"]
                error = "0.2" if name == "impaired" else "0"
                receiver_cmd = [str(PROJECT / "build/receiver"), *common,
                                "--output", str(output), "--ack-error", error,
                                "--ack-loss", error, "--seed", "42"]
                receiver = subprocess.Popen(receiver_cmd, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, text=True, env=env)
                try:
                    time.sleep(0.2)
                    sender = subprocess.run(
                        [str(PROJECT / "build/sender"), *common, "--input", str(source),
                         "--data-error", error, "--data-loss", error, "--seed", "21"],
                        capture_output=True, text=True, timeout=30, env=env)
                    receiver_out, receiver_err = receiver.communicate(timeout=30)
                    if receiver.returncode or sender.returncode:
                        raise RuntimeError(f"{protocol}/{name}: {sender.stderr} {receiver_err}")
                    if output.read_bytes() != data:
                        raise RuntimeError(f"{protocol}/{name}: received bytes differ")
                    print(f"PASS {protocol}/{name}: {sender.stdout.strip()}", flush=True)
                    count += 1
                finally:
                    if receiver.poll() is None:
                        receiver.kill()
                        receiver.communicate()
    print(f"{count} end-to-end transfers passed")


if __name__ == "__main__":
    main()
