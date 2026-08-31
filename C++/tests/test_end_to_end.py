"""End-to-end sender/receiver transfer checks over a real TCP connection.

Each case launches ``build/receiver`` as a subprocess, runs ``build/sender``
against it, and requires the reconstructed file to be byte-identical to
``test_data/input.bin``. Impairment cases pin the channel seed so every run
replays the same loss, corruption, and timeout schedule.
"""

from __future__ import annotations

import socket
import subprocess
import sys
import tempfile
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
SENDER = CPP_DIR / "build" / "sender"
RECEIVER = CPP_DIR / "build" / "receiver"
INPUT_PATH = CPP_DIR / "test_data" / "input.bin"

# Mirrors flow_control::metrics_csv_header() in src/metrics.cpp.
METRICS_COLUMNS = [
    "unique_payload_bytes",
    "transmitted_frame_bytes",
    "original_transmissions",
    "retransmissions",
    "acks",
    "timeouts",
    "duplicates",
    "out_of_order",
    "completion_ms",
    "rtt_sample_count",
    "total_rtt_ms",
    "current_timeout_ms",
    "efficiency",
    "goodput_bytes_per_second",
    "mean_rtt_ms",
]

FCS_SCHEMES = ["checksum16", "crc8", "crc10", "crc16", "crc32"]
PROTOCOLS = ["stop-and-wait", "go-back-n", "selective-repeat"]
DEFAULT_WINDOW = {"stop-and-wait": 1, "go-back-n": 4, "selective-repeat": 4}

RECEIVER_TIMEOUT_SECONDS = 120
SENDER_TIMEOUT_SECONDS = 120


class TransferError(AssertionError):
    """Raised when a sender/receiver pair fails to complete a transfer."""


def free_port() -> int:
    """Returns a port that is currently unbound on the loopback interface."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def parse_metrics(stdout: str) -> dict[str, float]:
    """Parses the sender's single machine-readable CSV metrics row."""
    lines = [line for line in stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise TransferError(f"expected exactly one metrics row, got {lines!r}")

    values = lines[0].split(",")
    if len(values) != len(METRICS_COLUMNS):
        raise TransferError(f"expected {len(METRICS_COLUMNS)} metrics columns, got {lines[0]!r}")

    return {name: float(value) for name, value in zip(METRICS_COLUMNS, values)}


def run_transfer(
    protocol: str,
    *,
    fcs: str = "checksum16",
    window: int | None = None,
    payload: int = 46,
    data_error: float = 0.0,
    data_delay: float = 0.0,
    ack_error: float = 0.0,
    ack_delay: float = 0.0,
    seed: int = 20260831,
) -> tuple[dict[str, float], bytes]:
    """Runs one complete transfer and returns its metrics and output bytes."""
    port = free_port()
    if window is None:
        window = DEFAULT_WINDOW[protocol]

    with tempfile.TemporaryDirectory() as directory:
        output_path = Path(directory) / "received.bin"
        receiver = subprocess.Popen(
            [str(RECEIVER), "--port", str(port), "--output", str(output_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            ready = receiver.stderr.readline()
            if "listening" not in ready:
                receiver.wait(timeout=RECEIVER_TIMEOUT_SECONDS)
                raise TransferError(f"receiver never bound its port: {ready!r}{receiver.stderr.read()}")

            sender = subprocess.run(
                [
                    str(SENDER),
                    "--protocol", protocol,
                    "--fcs", fcs,
                    "--input", str(INPUT_PATH),
                    "--host", "127.0.0.1",
                    "--port", str(port),
                    "--window", str(window),
                    "--payload", str(payload),
                    "--data-error", str(data_error),
                    "--data-delay", str(data_delay),
                    "--ack-error", str(ack_error),
                    "--ack-delay", str(ack_delay),
                    "--seed", str(seed),
                ],
                capture_output=True,
                text=True,
                timeout=SENDER_TIMEOUT_SECONDS,
            )
            receiver_stdout, receiver_stderr = receiver.communicate(
                timeout=RECEIVER_TIMEOUT_SECONDS
            )
        finally:
            if receiver.poll() is None:
                receiver.kill()
                receiver.wait()

        if sender.returncode != 0:
            raise TransferError(f"sender exited {sender.returncode}: {sender.stderr}")
        if receiver.returncode != 0:
            raise TransferError(f"receiver exited {receiver.returncode}: {receiver_stderr}")
        if receiver_stdout.strip():
            raise TransferError(f"receiver wrote unexpected stdout: {receiver_stdout!r}")

        return parse_metrics(sender.stdout), output_path.read_bytes()


def run_sender_expecting_failure(arguments: list[str]) -> subprocess.CompletedProcess:
    """Runs the sender with no receiver present and returns its result."""
    return subprocess.run(
        [str(SENDER), *arguments],
        capture_output=True,
        text=True,
        timeout=SENDER_TIMEOUT_SECONDS,
    )


class Suite:
    """Collects pass/fail results with the project's plain reporting style."""

    def __init__(self) -> None:
        self.failures = 0

    def check(self, name: str, condition: bool, detail: str = "") -> None:
        if condition:
            print(f"PASS: {name}")
            return
        self.failures += 1
        print(f"FAIL: {name}{f' -- {detail}' if detail else ''}", file=sys.stderr)

    def case(self, name: str, body) -> None:
        try:
            body()
        except Exception as error:  # noqa: BLE001 - reported as a test failure
            self.failures += 1
            print(f"FAIL: {name} -- {error}", file=sys.stderr)
        else:
            print(f"PASS: {name}")


def main() -> int:
    for binary in (SENDER, RECEIVER):
        if not binary.exists():
            print(f"FAIL: missing binary {binary}", file=sys.stderr)
            return 1
    if not INPUT_PATH.exists():
        print(f"FAIL: missing fixture {INPUT_PATH}", file=sys.stderr)
        return 1

    expected = INPUT_PATH.read_bytes()
    suite = Suite()

    def clean_transfer(protocol: str, fcs: str) -> None:
        metrics, received = run_transfer(protocol, fcs=fcs)
        if received != expected:
            raise TransferError("output file is not byte-identical to the input")
        if metrics["unique_payload_bytes"] != len(expected):
            raise TransferError(f"unique payload bytes {metrics['unique_payload_bytes']}")
        if metrics["retransmissions"] != 0 or metrics["timeouts"] != 0:
            raise TransferError("an unimpaired transfer retransmitted or timed out")

    for protocol in PROTOCOLS:
        for fcs in FCS_SCHEMES:
            suite.case(
                f"clean transfer {protocol} with {fcs}",
                lambda protocol=protocol, fcs=fcs: clean_transfer(protocol, fcs),
            )

    def payload_transfer(protocol: str, fcs: str, payload: int) -> None:
        _, received = run_transfer(protocol, fcs=fcs, payload=payload)
        if received != expected:
            raise TransferError("output file is not byte-identical to the input")

    # 4096 bytes is not a multiple of 46 or 100, so both leave a short final
    # frame; 64 divides it exactly and 1499 is the CRC-32 payload boundary.
    suite.case(
        "short final frame with a 46-byte payload",
        lambda: payload_transfer("go-back-n", "crc16", 46),
    )
    suite.case(
        "short final frame with a 100-byte payload",
        lambda: payload_transfer("selective-repeat", "crc32", 100),
    )
    suite.case(
        "exact frame multiple with a 64-byte payload",
        lambda: payload_transfer("go-back-n", "crc8", 64),
    )
    suite.case(
        "maximum 1499-byte payload with crc32",
        lambda: payload_transfer("selective-repeat", "crc32", 1499),
    )

    def impaired_transfer(protocol: str, description: str, **impairment) -> None:
        metrics, received = run_transfer(protocol, **impairment)
        if received != expected:
            raise TransferError(f"{description} output is not byte-identical to the input")
        if len(received) != len(expected):
            raise TransferError(f"{description} wrote {len(received)} of {len(expected)} bytes")
        if metrics["retransmissions"] <= 0:
            raise TransferError(f"{description} never retransmitted")
        if metrics["timeouts"] <= 0:
            raise TransferError(f"{description} never timed out")

    for protocol in PROTOCOLS:
        suite.case(
            f"deterministic DATA loss {protocol}",
            lambda protocol=protocol: impaired_transfer(
                protocol, "DATA loss", data_delay=0.3, seed=11
            ),
        )
    for protocol in PROTOCOLS:
        suite.case(
            f"deterministic DATA corruption {protocol}",
            lambda protocol=protocol: impaired_transfer(
                protocol, "DATA corruption", data_error=0.3, seed=13
            ),
        )
    for protocol in PROTOCOLS:
        suite.case(
            f"deterministic ACK loss {protocol}",
            lambda protocol=protocol: impaired_transfer(
                protocol, "ACK loss", ack_delay=0.3, seed=17
            ),
        )
    suite.case(
        "deterministic ACK corruption selective-repeat",
        lambda: impaired_transfer(
            "selective-repeat", "ACK corruption", ack_error=0.3, seed=19
        ),
    )

    def duplicate_safe(protocol: str) -> None:
        metrics, received = run_transfer(
            protocol,
            fcs="crc32",
            window=8 if protocol != "stop-and-wait" else 1,
            data_error=0.2,
            data_delay=0.2,
            ack_error=0.2,
            ack_delay=0.2,
            seed=23,
        )
        if received != expected:
            raise TransferError("retransmission produced a non-identical output file")
        if metrics["retransmissions"] <= 0:
            raise TransferError("heavy impairment never retransmitted")

    for protocol in PROTOCOLS:
        suite.case(
            f"duplicate-safe output under heavy impairment {protocol}",
            lambda protocol=protocol: duplicate_safe(protocol),
        )

    def deterministic_replay() -> None:
        first, first_bytes = run_transfer(
            "go-back-n", data_error=0.25, ack_delay=0.25, seed=29
        )
        second, second_bytes = run_transfer(
            "go-back-n", data_error=0.25, ack_delay=0.25, seed=29
        )
        if first_bytes != expected or second_bytes != expected:
            raise TransferError("a replayed transfer was not byte-identical")
        counters = [
            name for name in METRICS_COLUMNS
            if name not in {"completion_ms", "total_rtt_ms", "goodput_bytes_per_second", "mean_rtt_ms"}
        ]
        for name in counters:
            if first[name] != second[name]:
                raise TransferError(f"{name} differed between identical seeded runs")

    suite.case("identical seeds replay identical counters", deterministic_replay)

    rejections = [
        ("unknown protocol name", ["--protocol", "sliding-window", "--input", str(INPUT_PATH), "--port", "9"]),
        ("unknown FCS scheme name", ["--protocol", "go-back-n", "--fcs", "crc7", "--input", str(INPUT_PATH), "--port", "9"]),
        ("go-back-n window above 255", ["--protocol", "go-back-n", "--input", str(INPUT_PATH), "--port", "9", "--window", "300"]),
        ("selective-repeat window above 128", ["--protocol", "selective-repeat", "--input", str(INPUT_PATH), "--port", "9", "--window", "200"]),
        ("zero window", ["--protocol", "go-back-n", "--input", str(INPUT_PATH), "--port", "9", "--window", "0"]),
        ("probability above one", ["--protocol", "go-back-n", "--input", str(INPUT_PATH), "--port", "9", "--data-error", "1.5"]),
        ("negative probability", ["--protocol", "go-back-n", "--input", str(INPUT_PATH), "--port", "9", "--ack-delay", "-0.1"]),
        ("payload above the frame limit", ["--protocol", "go-back-n", "--input", str(INPUT_PATH), "--port", "9", "--payload", "1500"]),
        ("port out of range", ["--protocol", "go-back-n", "--input", str(INPUT_PATH), "--port", "70000"]),
        ("missing required input", ["--protocol", "go-back-n", "--port", "9"]),
        ("missing option value", ["--protocol"]),
        ("unknown option", ["--protocol", "go-back-n", "--input", str(INPUT_PATH), "--port", "9", "--turbo", "1"]),
    ]
    for name, arguments in rejections:
        result = run_sender_expecting_failure(arguments)
        suite.check(
            f"sender rejects {name}",
            result.returncode != 0 and bool(result.stderr.strip()) and not result.stdout.strip(),
            f"exit {result.returncode}, stdout {result.stdout!r}",
        )

    receiver_rejections = [
        ("missing output path", ["--port", "9"]),
        ("port out of range", ["--port", "0", "--output", "/dev/null"]),
        ("unknown option", ["--port", "9", "--output", "/dev/null", "--verbose", "1"]),
    ]
    for name, arguments in receiver_rejections:
        result = subprocess.run(
            [str(RECEIVER), *arguments],
            capture_output=True,
            text=True,
            timeout=SENDER_TIMEOUT_SECONDS,
        )
        suite.check(
            f"receiver rejects {name}",
            result.returncode != 0 and bool(result.stderr.strip()) and not result.stdout.strip(),
            f"exit {result.returncode}, stdout {result.stdout!r}",
        )

    if suite.failures != 0:
        print(f"FAIL: {suite.failures} end-to-end case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
