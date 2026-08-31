"""Identity and denominator checks for ``tools/validate_results.py``.

Every case builds a small synthetic ``experiments.csv`` that is internally
consistent, confirms the validator accepts it, then mutates exactly one field
and requires the validator to reject the file and name the offending run. A
validator that cannot fail is worthless, so the mutations are the real test.
"""

from __future__ import annotations

import csv
import subprocess
import sys
import tempfile
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CPP_DIR / "tools"))

import run_experiments as runner  # noqa: E402 - path is configured above
import validate_results as validator  # noqa: E402 - path is configured above

VALIDATOR = CPP_DIR / "tools" / "validate_results.py"
REAL_RESULTS = CPP_DIR / "results" / "experiments.csv"

INPUT_BYTES = 4096
PAYLOAD_BYTES = 46
FRAME_BYTES = 64  # 15-byte header + 46-byte payload + 2-byte CRC-16, padded to 64.
FRAME_COUNT = -(-INPUT_BYTES // PAYLOAD_BYTES)  # ceil division

TOOL_TIMEOUT_SECONDS = 60


class ValidatorError(AssertionError):
    """Raised when the validator accepts bad data or rejects good data."""


def synthetic_metrics(run: runner.ExperimentRun) -> dict[str, float]:
    """Builds one internally consistent metrics dictionary for a run."""
    # Impairment scales retransmissions so the fixture looks like real data.
    retransmissions = int(round(run.probability * 100))
    transmissions = FRAME_COUNT + retransmissions
    transmitted = transmissions * FRAME_BYTES
    completion = 90 + retransmissions * 10
    rtt_samples = FRAME_COUNT - retransmissions if retransmissions < FRAME_COUNT else 0
    total_rtt = rtt_samples * 2
    return {
        "unique_payload_bytes": float(INPUT_BYTES),
        "transmitted_frame_bytes": float(transmitted),
        "original_transmissions": float(FRAME_COUNT),
        "retransmissions": float(retransmissions),
        "acks": float(FRAME_COUNT + retransmissions),
        "timeouts": float(retransmissions),
        "duplicates": 0.0,
        "out_of_order": 0.0,
        "completion_ms": float(completion),
        "rtt_sample_count": float(rtt_samples),
        "total_rtt_ms": float(total_rtt),
        "current_timeout_ms": 10.0,
        "efficiency": INPUT_BYTES / transmitted,
        "goodput_bytes_per_second": INPUT_BYTES * 1000.0 / completion,
        "mean_rtt_ms": (total_rtt / rtt_samples) if rtt_samples else 0.0,
    }


def synthetic_rows() -> list[list[str]]:
    """Builds a complete, internally consistent synthetic result set."""
    return [
        runner.result_row(run, synthetic_metrics(run))
        for run in runner.build_matrix(input_bytes=INPUT_BYTES)
    ]


def write_csv(path: Path, rows: list[list[str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(runner.EXPERIMENT_COLUMNS)
        writer.writerows(rows)


def run_validator(path: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(VALIDATOR), "--input", str(path)],
        capture_output=True,
        text=True,
        timeout=TOOL_TIMEOUT_SECONDS,
    )


def mutate(rows: list[list[str]], run_id: str, column: str, value: str) -> list[list[str]]:
    """Returns a copy of ``rows`` with one field of one run replaced."""
    index = runner.EXPERIMENT_COLUMNS.index(column)
    copied = [list(row) for row in rows]
    for row in copied:
        if row[0] == run_id:
            row[index] = value
            return copied
    raise ValidatorError(f"no run named {run_id} to mutate")


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
    if not VALIDATOR.exists():
        print(f"FAIL: missing tool {VALIDATOR}", file=sys.stderr)
        return 1

    suite = Suite()
    rows = synthetic_rows()

    with tempfile.TemporaryDirectory() as raw_directory:
        directory = Path(raw_directory)
        good = directory / "good.csv"
        write_csv(good, rows)

        def accepts_consistent_results() -> None:
            result = run_validator(good)
            if result.returncode != 0:
                raise ValidatorError(
                    f"consistent results were rejected: {result.stdout}{result.stderr}"
                )

        suite.case("a consistent result set is accepted", accepts_consistent_results)

        def efficiency_definition_is_stated() -> None:
            text = validator.EFFICIENCY_DEFINITION
            if "unique_payload_bytes" not in text or "transmitted_frame_bytes" not in text:
                raise ValidatorError(f"efficiency definition is unclear: {text!r}")

        suite.case(
            "the efficiency definition is stated in one place",
            efficiency_definition_is_stated,
        )

        # Each mutation breaks exactly one identity and must be caught by name.
        mutations = [
            (
                "delivered payload above the input size",
                "stop-and-wait__none",
                "unique_payload_bytes",
                str(INPUT_BYTES + 1),
            ),
            (
                "an unimpaired run that lost payload",
                "go-back-n__none",
                "unique_payload_bytes",
                str(INPUT_BYTES - 46),
            ),
            (
                "fewer original transmissions than the input needs",
                "selective-repeat__none",
                "original_transmissions",
                str(FRAME_COUNT - 1),
            ),
            (
                "a negative retransmission count",
                "go-back-n__data-error__0.3",
                "retransmissions",
                "-1",
            ),
            (
                "a zero completion time",
                "stop-and-wait__ack-delay__0.2",
                "completion_ms",
                "0",
            ),
            (
                "a zero efficiency denominator",
                "selective-repeat__data-delay__0.4",
                "transmitted_frame_bytes",
                "0",
            ),
            (
                "an efficiency that contradicts its definition",
                "go-back-n__ack-error__0.5",
                "efficiency",
                "0.999999",
            ),
            (
                "a goodput that contradicts its denominator",
                "stop-and-wait__data-error__0.1",
                "goodput_bytes_per_second",
                "1.000000",
            ),
            (
                "a mean RTT that contradicts its samples",
                "selective-repeat__ack-error__0.1",
                "mean_rtt_ms",
                "42.000000",
            ),
            (
                "a nonzero mean RTT with no samples",
                "go-back-n__data-delay__0.2",
                "rtt_sample_count",
                "0",
            ),
            (
                "transmitted bytes that do not match the frame count",
                "stop-and-wait__data-delay__0.3",
                "transmitted_frame_bytes",
                str(FRAME_BYTES * (FRAME_COUNT + 30) + 1),
            ),
            (
                "more duplicate ACKs than ACKs",
                "selective-repeat__data-error__0.2",
                "duplicates",
                "100000",
            ),
            (
                "an unimpaired run that retransmitted",
                "stop-and-wait__none",
                "retransmissions",
                "3",
            ),
            (
                "an unimpaired run that timed out",
                "go-back-n__none",
                "timeouts",
                "1",
            ),
            (
                "a zero timeout value",
                "selective-repeat__ack-delay__0.5",
                "current_timeout_ms",
                "0",
            ),
            (
                "a non-numeric counter",
                "go-back-n__ack-delay__0.1",
                "acks",
                "many",
            ),
        ]
        for name, run_id, column, value in mutations:
            broken = directory / "broken.csv"
            write_csv(broken, mutate(rows, run_id, column, value))
            result = run_validator(broken)
            output = result.stdout + result.stderr
            suite.check(
                f"validator rejects {name}",
                result.returncode != 0 and run_id in output,
                f"exit {result.returncode}, output {output!r}",
            )

        def rejects_a_missing_probability_level() -> None:
            reduced = [row for row in rows if row[0] != "go-back-n__data-error__0.3"]
            path = directory / "incomplete.csv"
            write_csv(path, reduced)
            result = run_validator(path)
            output = result.stdout + result.stderr
            if result.returncode == 0:
                raise ValidatorError("an incomplete probability matrix was accepted")
            if "data-error" not in output or "go-back-n" not in output:
                raise ValidatorError(f"the gap was not identified: {output!r}")

        suite.case(
            "validator rejects an incomplete 0.1-0.5 probability matrix",
            rejects_a_missing_probability_level,
        )

        def rejects_a_missing_protocol() -> None:
            reduced = [row for row in rows if not row[0].startswith("selective-repeat")]
            path = directory / "one_protocol_missing.csv"
            write_csv(path, reduced)
            if run_validator(path).returncode == 0:
                raise ValidatorError("a result set missing a protocol was accepted")

        suite.case("validator rejects a missing protocol", rejects_a_missing_protocol)

        def rejects_a_duplicate_run() -> None:
            path = directory / "duplicate.csv"
            write_csv(path, rows + [rows[0]])
            if run_validator(path).returncode == 0:
                raise ValidatorError("a duplicated run was accepted")

        suite.case("validator rejects a duplicated run", rejects_a_duplicate_run)

        def rejects_a_wrong_header() -> None:
            path = directory / "bad_header.csv"
            with path.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle, lineterminator="\n")
                writer.writerow(runner.EXPERIMENT_COLUMNS[:-1])
                writer.writerows(row[:-1] for row in rows)
            if run_validator(path).returncode == 0:
                raise ValidatorError("a CSV with the wrong header was accepted")

        suite.case("validator rejects an unexpected header", rejects_a_wrong_header)

        def rejects_an_empty_file() -> None:
            path = directory / "empty.csv"
            path.write_text("", encoding="utf-8")
            if run_validator(path).returncode == 0:
                raise ValidatorError("an empty file was accepted")

        suite.case("validator rejects an empty file", rejects_an_empty_file)

        def rejects_a_missing_file() -> None:
            if run_validator(directory / "absent.csv").returncode == 0:
                raise ValidatorError("a missing file was accepted")

        suite.case("validator rejects a missing file", rejects_a_missing_file)

        def flags_unmeasurable_rtt_without_failing() -> None:
            # rtt_sample_count == 0 makes the mean-RTT denominator zero. Such a
            # row must be flagged for exclusion, not silently treated as 0 ms.
            flagged = mutate(rows, "go-back-n__data-error__0.5", "rtt_sample_count", "0")
            flagged = mutate(flagged, "go-back-n__data-error__0.5", "total_rtt_ms", "0")
            flagged = mutate(flagged, "go-back-n__data-error__0.5", "mean_rtt_ms", "0.000000")
            path = directory / "flagged.csv"
            write_csv(path, flagged)
            result = run_validator(path)
            if result.returncode != 0:
                raise ValidatorError(f"a legitimately unmeasured RTT failed: {result.stderr}")
            if "go-back-n__data-error__0.5" not in result.stdout:
                raise ValidatorError("an unmeasurable mean RTT was not flagged")

        suite.case(
            "an unmeasurable mean RTT is flagged, not silently zeroed",
            flags_unmeasurable_rtt_without_failing,
        )

    def accepts_the_real_results() -> None:
        if not REAL_RESULTS.exists():
            raise ValidatorError(f"missing {REAL_RESULTS}; run the experiments first")
        result = run_validator(REAL_RESULTS)
        if result.returncode != 0:
            raise ValidatorError(
                f"the committed results failed validation: {result.stdout}{result.stderr}"
            )

    suite.case("the committed experiment results validate", accepts_the_real_results)

    if suite.failures != 0:
        print(f"FAIL: {suite.failures} validation case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
