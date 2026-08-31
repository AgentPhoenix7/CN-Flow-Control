#!/usr/bin/env python3
"""Runs the reproducible protocol/impairment experiment matrix.

Each run launches ``build/receiver`` and then ``build/sender`` as subprocesses
over a loopback TCP connection, exactly as ``tests/test_end_to_end.py`` does,
captures the single machine-readable metrics row the sender prints on success,
and appends it to ``results/experiments.csv`` alongside the matrix metadata that
identifies the run.

Matrix design
-------------
The guideline asks for all three protocols with no impairment and with
probabilities 0.1-0.5 for errors and for delays, independently affecting the
DATA and ACK paths, using the same input, payload size, window sizes, seeds, and
impairment schedule across comparisons.

That is a **one-factor-at-a-time** matrix: every run holds three of the four
impairment probabilities at zero and sweeps the fourth. A full cross product of
four dimensions at five levels would be 5^4 = 625 combinations per protocol and
would not isolate any single path's effect, which is what the comparison is for.
The four dimensions are swept independently instead, giving
``3 protocols x (1 baseline + 4 paths x 5 probabilities) = 63`` runs.

Everything not being varied is pinned so the columns are comparable:

* ``--fcs crc16`` -- CRC-16 (``0x8005``) has a two-byte FCS and far stronger
  burst detection than Checksum-16 or CRC-8/10, without CRC-32's four-byte
  overhead skewing the efficiency ratio. ``tests/test_end_to_end.py`` already
  proves every protocol is byte-exact under all five schemes, so the experiment
  fixes one scheme and varies the protocol instead.
* ``--window 8`` for Go-Back-N and Selective Repeat -- inside both protocol
  limits (Go-Back-N <= 255, Selective Repeat <= 128), and small relative to the
  90-frame transfer, so the window fills and drains many times and the
  go-back-N penalty is visible. Stop-and-Wait is one outstanding frame by
  definition and the sender forces ``--window 1`` for it regardless.
* ``--payload 46`` -- safe for every FCS scheme and the professor's suggested
  size. 4096 / 46 = 90 frames, the last of which is short.
* ``--seed 20260831`` -- one seed for every run, so each protocol meets the same
  channel decision sequence.

Note on "loss" versus "delay": the sender exposes one error probability and one
excessive-delay probability per path. There is no separate drop flag, because
excessive delay *is* how this system models loss -- a delayed frame or ACK never
arrives before its timeout and is suppressed for that round. The ``data-delay``
and ``ack-delay`` sweeps are therefore the loss sweeps.

Usage:
    uv run python C++/tools/run_experiments.py [--output <csv>] [--input <bin>]
"""

from __future__ import annotations

import argparse
import csv
import socket
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent

DEFAULT_SENDER = CPP_DIR / "build" / "sender"
DEFAULT_RECEIVER = CPP_DIR / "build" / "receiver"
DEFAULT_INPUT = CPP_DIR / "test_data" / "input.bin"
DEFAULT_OUTPUT = CPP_DIR / "results" / "experiments.csv"

#: Mirrors flow_control::metrics_csv_header() in src/metrics.cpp.
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

#: Matrix metadata written ahead of the sender's own columns.
METADATA_COLUMNS = [
    "run_id",
    "protocol",
    "fcs",
    "window",
    "payload_bytes",
    "input_bytes",
    "seed",
    "impairment",
    "probability",
]

EXPERIMENT_COLUMNS = METADATA_COLUMNS + METRICS_COLUMNS

PROTOCOLS = ["stop-and-wait", "go-back-n", "selective-repeat"]

#: The four independently swept impairment paths, mapped to sender flags.
IMPAIRMENTS = ["data-error", "data-delay", "ack-error", "ack-delay"]

#: Probability levels required by the evaluation guideline.
PROBABILITIES = [0.1, 0.2, 0.3, 0.4, 0.5]

FCS_SCHEME = "crc16"
PAYLOAD_BYTES = 46
SEED = 20260831
SLIDING_WINDOW = 8
#: Stop-and-Wait keeps exactly one frame outstanding by definition.
PROTOCOL_WINDOW = {
    "stop-and-wait": 1,
    "go-back-n": SLIDING_WINDOW,
    "selective-repeat": SLIDING_WINDOW,
}

RECEIVER_TIMEOUT_SECONDS = 300
SENDER_TIMEOUT_SECONDS = 300


class ExperimentError(RuntimeError):
    """Raised when a run fails; aborts the whole matrix."""


class MetricsError(ExperimentError):
    """Raised when the sender's stdout is not one well-formed metrics row."""


@dataclass(frozen=True)
class ExperimentRun:
    """One point of the matrix: a protocol under one impairment level."""

    protocol: str
    fcs: str
    window: int
    payload_bytes: int
    input_bytes: int
    seed: int
    impairment: str
    probability: float

    def probabilities(self) -> dict[str, float]:
        """Returns all four path probabilities, only this run's factor nonzero."""
        values = {name: 0.0 for name in IMPAIRMENTS}
        if self.impairment != "none":
            values[self.impairment] = self.probability
        return values

    def run_id(self) -> str:
        """Returns a stable, unique, filesystem-safe identifier for this run."""
        if self.impairment == "none":
            return f"{self.protocol}__none"
        return f"{self.protocol}__{self.impairment}__{self.probability:.1f}"


def build_matrix(input_bytes: int = 0) -> list[ExperimentRun]:
    """Returns the full one-factor-at-a-time matrix in a stable order."""
    runs: list[ExperimentRun] = []
    for protocol in PROTOCOLS:
        window = PROTOCOL_WINDOW[protocol]
        common = {
            "protocol": protocol,
            "fcs": FCS_SCHEME,
            "window": window,
            "payload_bytes": PAYLOAD_BYTES,
            "input_bytes": input_bytes,
            "seed": SEED,
        }
        runs.append(ExperimentRun(**common, impairment="none", probability=0.0))
        for impairment in IMPAIRMENTS:
            for probability in PROBABILITIES:
                runs.append(
                    ExperimentRun(**common, impairment=impairment, probability=probability)
                )
    return runs


def sender_arguments(
    run: ExperimentRun, *, sender: str, input_path: str, host: str, port: int
) -> list[str]:
    """Builds the sender command line for one run."""
    probabilities = run.probabilities()
    return [
        sender,
        "--protocol", run.protocol,
        "--fcs", run.fcs,
        "--input", input_path,
        "--host", host,
        "--port", str(port),
        "--window", str(run.window),
        "--payload", str(run.payload_bytes),
        "--data-error", str(probabilities["data-error"]),
        "--data-delay", str(probabilities["data-delay"]),
        "--ack-error", str(probabilities["ack-error"]),
        "--ack-delay", str(probabilities["ack-delay"]),
        "--seed", str(run.seed),
    ]


def parse_metrics_row(stdout: str) -> dict[str, float]:
    """Parses the sender's single machine-readable CSV metrics row."""
    lines = [line for line in stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise MetricsError(f"expected exactly one metrics row, got {lines!r}")

    values = lines[0].split(",")
    if len(values) != len(METRICS_COLUMNS):
        raise MetricsError(
            f"expected {len(METRICS_COLUMNS)} metrics columns, got {lines[0]!r}"
        )

    parsed: dict[str, float] = {}
    for name, value in zip(METRICS_COLUMNS, values):
        try:
            parsed[name] = float(value)
        except ValueError as error:
            raise MetricsError(f"column {name} is not a number: {value!r}") from error
    return parsed


def result_row(run: ExperimentRun, metrics: dict[str, float]) -> list[str]:
    """Assembles one CSV row of matrix metadata followed by sender metrics."""
    missing = [name for name in METRICS_COLUMNS if name not in metrics]
    if missing:
        raise MetricsError(f"metrics are missing columns {missing}")

    metadata = [
        run.run_id(),
        run.protocol,
        run.fcs,
        str(run.window),
        str(run.payload_bytes),
        str(run.input_bytes),
        str(run.seed),
        run.impairment,
        f"{run.probability:.1f}",
    ]
    return metadata + [format_metric(name, metrics[name]) for name in METRICS_COLUMNS]


def format_metric(name: str, value: float) -> str:
    """Keeps counters integral and derived values at the sender's precision."""
    derived = {"efficiency", "goodput_bytes_per_second", "mean_rtt_ms"}
    if name in derived:
        return f"{value:.6f}"
    return str(int(value))


def free_port() -> int:
    """Returns a port that is currently unbound on the loopback interface."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def execute(
    run: ExperimentRun,
    *,
    sender: Path,
    receiver: Path,
    input_path: Path,
    expected: bytes,
    host: str = "127.0.0.1",
) -> dict[str, float]:
    """Runs one experiment and returns its parsed metrics.

    Raises ``ExperimentError`` on any nonzero subprocess status, on a metrics
    row that does not parse, or if the reconstructed file is not byte-identical
    to the input. A failed run aborts the matrix rather than producing a row
    downstream analysis would silently trust.
    """
    port = free_port()

    with tempfile.TemporaryDirectory() as directory:
        output_path = Path(directory) / "received.bin"
        receiver_process = subprocess.Popen(
            [str(receiver), "--port", str(port), "--output", str(output_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            ready = receiver_process.stderr.readline()
            if "listening" not in ready:
                receiver_process.wait(timeout=RECEIVER_TIMEOUT_SECONDS)
                raise ExperimentError(
                    f"{run.run_id()}: receiver never bound its port: {ready!r}"
                    f"{receiver_process.stderr.read()}"
                )

            sender_result = subprocess.run(
                sender_arguments(
                    run,
                    sender=str(sender),
                    input_path=str(input_path),
                    host=host,
                    port=port,
                ),
                capture_output=True,
                text=True,
                timeout=SENDER_TIMEOUT_SECONDS,
            )
            _, receiver_stderr = receiver_process.communicate(
                timeout=RECEIVER_TIMEOUT_SECONDS
            )
        finally:
            if receiver_process.poll() is None:
                receiver_process.kill()
                receiver_process.wait()

        if sender_result.returncode != 0:
            raise ExperimentError(
                f"{run.run_id()}: sender exited {sender_result.returncode}: "
                f"{sender_result.stderr}"
            )
        if receiver_process.returncode != 0:
            raise ExperimentError(
                f"{run.run_id()}: receiver exited {receiver_process.returncode}: "
                f"{receiver_stderr}"
            )
        if output_path.read_bytes() != expected:
            raise ExperimentError(
                f"{run.run_id()}: reconstructed file is not byte-identical to the input"
            )

        try:
            return parse_metrics_row(sender_result.stdout)
        except MetricsError as error:
            raise ExperimentError(f"{run.run_id()}: {error}") from error


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="run_experiments.py",
        description="Run the reproducible protocol/impairment experiment matrix.",
    )
    parser.add_argument("--sender", default=str(DEFAULT_SENDER))
    parser.add_argument("--receiver", default=str(DEFAULT_RECEIVER))
    parser.add_argument("--input", default=str(DEFAULT_INPUT))
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--host", default="127.0.0.1")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)

    sender = Path(arguments.sender)
    receiver = Path(arguments.receiver)
    input_path = Path(arguments.input)
    for required in (sender, receiver, input_path):
        if not required.exists():
            print(f"run_experiments: missing {required}", file=sys.stderr)
            return 1

    expected = input_path.read_bytes()
    matrix = build_matrix(input_bytes=len(expected))
    rows: list[list[str]] = []

    for index, run in enumerate(matrix, start=1):
        print(f"[{index}/{len(matrix)}] {run.run_id()}", flush=True)
        try:
            metrics = execute(
                run,
                sender=sender,
                receiver=receiver,
                input_path=input_path,
                expected=expected,
                host=arguments.host,
            )
        except (ExperimentError, subprocess.SubprocessError, OSError) as error:
            print(f"run_experiments: {error}", file=sys.stderr)
            return 1
        rows.append(result_row(run, metrics))

    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(EXPERIMENT_COLUMNS)
        writer.writerows(rows)

    print(f"{output}: {len(rows)} runs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
