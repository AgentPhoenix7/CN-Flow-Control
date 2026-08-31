#!/usr/bin/env python3
"""Validates experiment identities and denominators before anything trusts them.

This tool is the gate between ``results/experiments.csv`` and every downstream
consumer: the plots and the report both read the file only after it has passed
here. It fails loudly, with a nonzero exit status and a message naming the
offending run, rather than dropping or repairing a bad row.

Two kinds of problems are distinguished:

* **Violations** are contradictions -- a counter identity that does not hold, a
  derived value that disagrees with its own definition, or a zero denominator
  under a nonzero numerator. Any violation fails the run.
* **Flags** are rows whose derived value is genuinely not measurable, most
  importantly a ``mean_rtt_ms`` with ``rtt_sample_count == 0``. Under Go-Back-N
  at high impairment every frame is retransmitted before it is acknowledged, so
  Karn's rule discards every RTT sample and there is no mean to report. Such a
  row is reported on stdout and must be excluded from RTT analysis rather than
  charted as a real 0 ms.

Usage:
    uv run python C++/tools/validate_results.py [--input <csv>]
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CPP_DIR / "tools"))

from run_experiments import (  # noqa: E402 - path is configured above
    EXPERIMENT_COLUMNS,
    IMPAIRMENTS,
    METADATA_COLUMNS,
    METRICS_COLUMNS,
    PROBABILITIES,
    PROTOCOLS,
)

DEFAULT_INPUT = CPP_DIR / "results" / "experiments.csv"

#: The project's single definition of efficiency. Every tool that reports,
#: plots, or writes about efficiency imports this string rather than restating
#: it, so the definition cannot drift between the validator, the plots, and the
#: report. It matches flow_control::Metrics::efficiency() in src/metrics.cpp.
EFFICIENCY_DEFINITION = (
    "efficiency = unique_payload_bytes / transmitted_frame_bytes: the unique "
    "application payload the receiver accepted and delivered, divided by every "
    "DATA-frame byte the sender put on the wire, including retransmissions, the "
    "15-byte header, zero padding, and the FCS. It is a dimensionless ratio in "
    "(0, 1]; 1.0 would mean every transmitted byte was useful payload delivered "
    "exactly once. ACK traffic is not counted, because the sender measures only "
    "what it transmits on the DATA path."
)

#: Serialized FCS size per scheme, from C++/include/config.hpp.
FCS_SIZES = {"checksum16": 2, "crc8": 1, "crc10": 2, "crc16": 2, "crc32": 4}
FRAME_HEADER_SIZE = 15
MIN_FRAME_SIZE = 64

#: Counters the sender emits as unsigned integers.
COUNTER_COLUMNS = [
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
]

#: Values the sender derives from those counters.
DERIVED_COLUMNS = ["efficiency", "goodput_bytes_per_second", "mean_rtt_ms"]

#: Comparison tolerance for a value the sender printed to six decimals.
RELATIVE_TOLERANCE = 1e-6
ABSOLUTE_TOLERANCE = 1e-6

WINDOW_LIMIT = {"stop-and-wait": 1, "go-back-n": 255, "selective-repeat": 128}


class ResultsError(Exception):
    """Raised when the results file cannot be read as the expected schema."""


def close_enough(actual: float, expected: float) -> bool:
    return math.isclose(
        actual, expected, rel_tol=RELATIVE_TOLERANCE, abs_tol=ABSOLUTE_TOLERANCE
    )


def frame_wire_size(fcs: str, payload_bytes: int) -> int:
    """Returns the serialized size of one DATA frame, padding included."""
    return max(MIN_FRAME_SIZE, FRAME_HEADER_SIZE + payload_bytes + FCS_SIZES[fcs])


def load_rows(path: Path) -> list[dict[str, str]]:
    """Reads the results file and checks its header against the schema."""
    if not path.exists():
        raise ResultsError(f"missing results file: {path}")

    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration as error:
            raise ResultsError(f"{path} is empty") from error
        if header != EXPERIMENT_COLUMNS:
            raise ResultsError(
                f"{path} header is {header}, expected {EXPERIMENT_COLUMNS}"
            )
        rows = [row for row in reader if row]

    if not rows:
        raise ResultsError(f"{path} contains no result rows")
    for number, row in enumerate(rows, start=2):
        if len(row) != len(EXPERIMENT_COLUMNS):
            raise ResultsError(
                f"{path} line {number} has {len(row)} fields, "
                f"expected {len(EXPERIMENT_COLUMNS)}"
            )
    return [dict(zip(EXPERIMENT_COLUMNS, row)) for row in rows]


def validate_row(row: dict[str, str]) -> tuple[list[str], list[str]]:
    """Checks one run's identities. Returns its violations and its flags."""
    run_id = row["run_id"] or "<unnamed run>"
    violations: list[str] = []
    flags: list[str] = []

    def fail(message: str) -> None:
        violations.append(f"{run_id}: {message}")

    # --- The row must first be readable as the types the schema promises. ---
    numbers: dict[str, float] = {}
    for column in COUNTER_COLUMNS:
        text = row[column]
        try:
            value = int(text)
        except ValueError:
            fail(f"{column} is not an integer: {text!r}")
            continue
        if value < 0:
            fail(f"{column} is negative: {value}")
        numbers[column] = float(value)
    for column in DERIVED_COLUMNS:
        text = row[column]
        try:
            value = float(text)
        except ValueError:
            fail(f"{column} is not a number: {text!r}")
            continue
        if not math.isfinite(value):
            fail(f"{column} is not finite: {text!r}")
            continue
        numbers[column] = value

    metadata: dict[str, int] = {}
    for column in ("window", "payload_bytes", "input_bytes", "seed"):
        try:
            metadata[column] = int(row[column])
        except ValueError:
            fail(f"{column} is not an integer: {row[column]!r}")
    try:
        probability = float(row["probability"])
    except ValueError:
        fail(f"probability is not a number: {row['probability']!r}")
        probability = -1.0

    if len(numbers) != len(COUNTER_COLUMNS) + len(DERIVED_COLUMNS) or len(metadata) != 4:
        # Later identities would compare against values that failed to parse.
        return violations, flags

    protocol = row["protocol"]
    fcs = row["fcs"]
    impairment = row["impairment"]

    if protocol not in PROTOCOLS:
        fail(f"unknown protocol {protocol!r}")
        return violations, flags
    if fcs not in FCS_SIZES:
        fail(f"unknown FCS scheme {fcs!r}")
        return violations, flags
    if impairment != "none" and impairment not in IMPAIRMENTS:
        fail(f"unknown impairment {impairment!r}")
        return violations, flags

    # --- Matrix metadata. ---
    if impairment == "none":
        if probability != 0.0:
            fail(f"an unimpaired run carries probability {probability}")
    elif not any(close_enough(probability, level) for level in PROBABILITIES):
        fail(f"probability {probability} is not one of {PROBABILITIES}")

    window = metadata["window"]
    if not 1 <= window <= WINDOW_LIMIT[protocol]:
        fail(f"window {window} is outside the {protocol} limit of {WINDOW_LIMIT[protocol]}")

    payload_bytes = metadata["payload_bytes"]
    input_bytes = metadata["input_bytes"]
    if payload_bytes < 1:
        fail(f"payload_bytes is {payload_bytes}")
        return violations, flags
    if input_bytes < 1:
        fail(f"input_bytes is {input_bytes}")
        return violations, flags

    unique = numbers["unique_payload_bytes"]
    transmitted = numbers["transmitted_frame_bytes"]
    originals = numbers["original_transmissions"]
    retransmissions = numbers["retransmissions"]
    acks = numbers["acks"]
    duplicates = numbers["duplicates"]
    out_of_order = numbers["out_of_order"]
    completion = numbers["completion_ms"]
    rtt_samples = numbers["rtt_sample_count"]
    total_rtt = numbers["total_rtt_ms"]
    current_timeout = numbers["current_timeout_ms"]
    timeouts = numbers["timeouts"]

    # --- Payload delivery. ---
    if unique > input_bytes:
        fail(f"unique_payload_bytes {unique:.0f} exceeds the input size {input_bytes}")
    elif unique != input_bytes:
        fail(
            f"unique_payload_bytes {unique:.0f} is short of the input size "
            f"{input_bytes}; the transfer did not deliver the whole file"
        )

    # --- Transmission counters. ---
    expected_frames = -(-input_bytes // payload_bytes)  # ceil division
    if originals < expected_frames:
        fail(
            f"original_transmissions {originals:.0f} is fewer than the "
            f"{expected_frames} frames a {input_bytes}-byte input needs at "
            f"{payload_bytes} bytes per frame"
        )
    elif originals != expected_frames:
        fail(
            f"original_transmissions {originals:.0f} exceeds the {expected_frames} "
            "frames of the input; a frame was originally transmitted twice"
        )

    wire_size = frame_wire_size(fcs, payload_bytes)
    expected_bytes = (originals + retransmissions) * wire_size
    if transmitted != expected_bytes:
        fail(
            f"transmitted_frame_bytes {transmitted:.0f} does not equal "
            f"{originals + retransmissions:.0f} frames x {wire_size} wire bytes "
            f"= {expected_bytes:.0f}"
        )

    # --- Acknowledgment counters. ---
    if duplicates > acks:
        fail(f"duplicates {duplicates:.0f} exceeds acks {acks:.0f}")
    if out_of_order > acks - duplicates:
        fail(
            f"out_of_order {out_of_order:.0f} exceeds the "
            f"{acks - duplicates:.0f} acknowledgments that moved the window"
        )

    # --- An unimpaired run has nothing to recover from. ---
    if impairment == "none":
        for column, value in (
            ("retransmissions", retransmissions),
            ("timeouts", timeouts),
            ("duplicates", duplicates),
        ):
            if value != 0:
                fail(f"an unimpaired run reported {column} {value:.0f}")

    # --- Denominators, checked before any division is trusted. ---
    if completion <= 0:
        fail("completion_ms is not positive, so goodput has a zero denominator")
    if transmitted <= 0:
        fail("transmitted_frame_bytes is not positive, so efficiency has a zero denominator")
    if current_timeout <= 0:
        fail(f"current_timeout_ms is not positive: {current_timeout:.0f}")
    if total_rtt < rtt_samples:
        fail(
            f"total_rtt_ms {total_rtt:.0f} is below rtt_sample_count "
            f"{rtt_samples:.0f}; every sample is at least one millisecond"
        )

    # --- Derived values must agree with their own definitions. ---
    if transmitted > 0:
        expected_efficiency = unique / transmitted
        if not close_enough(numbers["efficiency"], expected_efficiency):
            fail(
                f"efficiency {numbers['efficiency']:.6f} contradicts its definition "
                f"({unique:.0f} / {transmitted:.0f} = {expected_efficiency:.6f})"
            )
    if completion > 0:
        expected_goodput = unique * 1000.0 / completion
        if not close_enough(numbers["goodput_bytes_per_second"], expected_goodput):
            fail(
                f"goodput_bytes_per_second {numbers['goodput_bytes_per_second']:.6f} "
                f"contradicts {unique:.0f} bytes / {completion:.0f} ms = "
                f"{expected_goodput:.6f}"
            )

    if rtt_samples > 0:
        expected_mean = total_rtt / rtt_samples
        if not close_enough(numbers["mean_rtt_ms"], expected_mean):
            fail(
                f"mean_rtt_ms {numbers['mean_rtt_ms']:.6f} contradicts "
                f"{total_rtt:.0f} ms / {rtt_samples:.0f} samples = {expected_mean:.6f}"
            )
    else:
        if total_rtt != 0:
            fail(f"total_rtt_ms is {total_rtt:.0f} with no RTT samples")
        if numbers["mean_rtt_ms"] != 0.0:
            fail(
                f"mean_rtt_ms is {numbers['mean_rtt_ms']:.6f} with no RTT samples; "
                "the mean has a zero denominator"
            )
        flags.append(
            f"{run_id}: no valid RTT sample (Karn's rule discarded every "
            "acknowledgment), so mean_rtt_ms is not measurable and this run must "
            "be excluded from RTT analysis rather than read as 0 ms"
        )

    return violations, flags


def validate_matrix(rows: list[dict[str, str]]) -> list[str]:
    """Checks cross-row structure: uniqueness, completeness, fixed parameters."""
    violations: list[str] = []

    seen_ids: set[str] = set()
    for row in rows:
        if row["run_id"] in seen_ids:
            violations.append(f"{row['run_id']}: appears more than once")
        seen_ids.add(row["run_id"])

    keys: set[tuple[str, str, str]] = set()
    for row in rows:
        key = (row["protocol"], row["impairment"], row["probability"])
        if key in keys:
            violations.append(
                f"matrix: {key[0]} {key[1]} at probability {key[2]} appears more than once"
            )
        keys.add(key)

    for protocol in PROTOCOLS:
        if (protocol, "none", "0.0") not in keys:
            violations.append(f"matrix: {protocol} has no unimpaired baseline run")
        for impairment in IMPAIRMENTS:
            for probability in PROBABILITIES:
                if (protocol, impairment, f"{probability:.1f}") not in keys:
                    violations.append(
                        f"matrix: {protocol} {impairment} is missing "
                        f"probability {probability:.1f}"
                    )

    for column in ("fcs", "payload_bytes", "input_bytes", "seed"):
        values = {row[column] for row in rows}
        if len(values) > 1:
            violations.append(
                f"matrix: runs are not comparable, {column} varies across "
                f"{sorted(values)}"
            )
    for protocol in PROTOCOLS:
        windows = {row["window"] for row in rows if row["protocol"] == protocol}
        if len(windows) > 1:
            violations.append(
                f"matrix: {protocol} runs are not comparable, window varies "
                f"across {sorted(windows)}"
            )

    return violations


def validate(rows: list[dict[str, str]]) -> tuple[list[str], list[str]]:
    """Validates every row and the matrix as a whole."""
    violations: list[str] = []
    flags: list[str] = []
    for row in rows:
        row_violations, row_flags = validate_row(row)
        violations.extend(row_violations)
        flags.extend(row_flags)
    violations.extend(validate_matrix(rows))
    return violations, flags


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="validate_results.py",
        description="Validate experiment identities and denominators.",
        epilog=EFFICIENCY_DEFINITION,
    )
    parser.add_argument("--input", default=str(DEFAULT_INPUT))
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)

    try:
        rows = load_rows(Path(arguments.input))
    except ResultsError as error:
        print(f"validate_results: {error}", file=sys.stderr)
        return 1

    violations, flags = validate(rows)

    for flag in flags:
        print(f"FLAG: {flag}")

    if violations:
        for violation in violations:
            print(f"VIOLATION: {violation}", file=sys.stderr)
        print(
            f"validate_results: {len(violations)} identity violation(s) in "
            f"{arguments.input}",
            file=sys.stderr,
        )
        return 1

    print(f"{arguments.input}: {len(rows)} runs satisfy every identity")
    print(f"efficiency definition: {EFFICIENCY_DEFINITION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
