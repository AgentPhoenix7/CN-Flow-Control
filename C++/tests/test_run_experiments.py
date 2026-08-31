"""Matrix-completeness and row-parsing checks for ``tools/run_experiments.py``.

The runner's subprocess orchestration is exercised for real by
``make -C C++ experiments``; mocking it here would test the mock. What is
unit-testable in isolation, and what these cases cover, is the pure logic the
orchestration depends on: which runs the matrix contains, that comparisons hold
every non-varied parameter fixed, how a sender command line is built, and how a
metrics row is parsed and rejected.
"""

from __future__ import annotations

import sys
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CPP_DIR / "tools"))

import run_experiments as runner  # noqa: E402 - path is configured above

EXPECTED_PROBABILITIES = [0.1, 0.2, 0.3, 0.4, 0.5]
EXPECTED_PROTOCOLS = ["stop-and-wait", "go-back-n", "selective-repeat"]
EXPECTED_IMPAIRMENTS = ["data-error", "data-delay", "ack-error", "ack-delay"]


class MatrixError(AssertionError):
    """Raised when the experiment matrix or its parsing is wrong."""


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
    suite = Suite()
    matrix = runner.build_matrix()

    def baseline_is_complete() -> None:
        baseline = [run for run in matrix if run.impairment == "none"]
        if len(baseline) != len(EXPECTED_PROTOCOLS):
            raise MatrixError(f"expected one baseline per protocol, got {len(baseline)}")
        if sorted(run.protocol for run in baseline) != sorted(EXPECTED_PROTOCOLS):
            raise MatrixError(f"baseline protocols are {[r.protocol for r in baseline]}")
        for run in baseline:
            if run.probability != 0.0:
                raise MatrixError(f"baseline {run.protocol} has probability {run.probability}")

    suite.case("every protocol has an unimpaired baseline run", baseline_is_complete)

    def probability_matrix_is_complete() -> None:
        for protocol in EXPECTED_PROTOCOLS:
            for impairment in EXPECTED_IMPAIRMENTS:
                found = sorted(
                    run.probability
                    for run in matrix
                    if run.protocol == protocol and run.impairment == impairment
                )
                if found != EXPECTED_PROBABILITIES:
                    raise MatrixError(
                        f"{protocol}/{impairment} probabilities are {found}, "
                        f"expected {EXPECTED_PROBABILITIES}"
                    )

    suite.case(
        "every protocol covers 0.1-0.5 on all four impairment paths",
        probability_matrix_is_complete,
    )

    def matrix_has_no_extra_runs() -> None:
        expected = len(EXPECTED_PROTOCOLS) * (
            1 + len(EXPECTED_IMPAIRMENTS) * len(EXPECTED_PROBABILITIES)
        )
        if len(matrix) != expected:
            raise MatrixError(f"expected {expected} runs, got {len(matrix)}")
        names = {run.impairment for run in matrix}
        if names != {"none", *EXPECTED_IMPAIRMENTS}:
            raise MatrixError(f"unexpected impairment names {sorted(names)}")

    suite.case("the matrix contains exactly the planned runs", matrix_has_no_extra_runs)

    def one_factor_at_a_time() -> None:
        for run in matrix:
            probabilities = run.probabilities()
            nonzero = {name: value for name, value in probabilities.items() if value != 0.0}
            if run.impairment == "none":
                if nonzero:
                    raise MatrixError(f"baseline run carries impairment {nonzero}")
                continue
            if list(nonzero) != [run.impairment]:
                raise MatrixError(
                    f"{run.impairment} run varies {sorted(nonzero)} instead of one factor"
                )
            if nonzero[run.impairment] != run.probability:
                raise MatrixError(f"{run.impairment} run set {nonzero} for {run.probability}")

    suite.case("each run varies exactly one impairment factor", one_factor_at_a_time)

    def shared_parameters_are_fixed() -> None:
        if len({run.fcs for run in matrix}) != 1:
            raise MatrixError("runs do not share one FCS scheme")
        if len({run.payload_bytes for run in matrix}) != 1:
            raise MatrixError("runs do not share one payload size")
        if len({run.seed for run in matrix}) != 1:
            raise MatrixError("runs do not share one seed")
        for protocol in EXPECTED_PROTOCOLS:
            windows = {run.window for run in matrix if run.protocol == protocol}
            if len(windows) != 1:
                raise MatrixError(f"{protocol} runs do not share one window: {windows}")

    suite.case(
        "input, FCS, payload, seed, and per-protocol window are held fixed",
        shared_parameters_are_fixed,
    )

    def windows_respect_protocol_limits() -> None:
        for run in matrix:
            if run.protocol == "stop-and-wait" and run.window != 1:
                raise MatrixError(f"stop-and-wait window is {run.window}, must be 1")
            if run.protocol == "go-back-n" and not 1 <= run.window <= 255:
                raise MatrixError(f"go-back-n window {run.window} exceeds 255")
            if run.protocol == "selective-repeat" and not 1 <= run.window <= 128:
                raise MatrixError(f"selective-repeat window {run.window} exceeds 128")

    suite.case("window sizes respect the protocol limits", windows_respect_protocol_limits)

    def run_ids_are_unique() -> None:
        identifiers = [run.run_id() for run in matrix]
        if len(set(identifiers)) != len(identifiers):
            raise MatrixError("two runs share a run identifier")

    suite.case("run identifiers are unique", run_ids_are_unique)

    def sender_command_line_is_correct() -> None:
        run = next(r for r in matrix if r.impairment == "ack-delay" and r.probability == 0.3)
        arguments = runner.sender_arguments(
            run, sender="/bin/sender", input_path="/data/in.bin", host="127.0.0.1", port=5000
        )
        if arguments[0] != "/bin/sender":
            raise MatrixError(f"first argument is {arguments[0]!r}")
        pairs = dict(zip(arguments[1::2], arguments[2::2]))
        expected = {
            "--protocol": run.protocol,
            "--fcs": run.fcs,
            "--input": "/data/in.bin",
            "--host": "127.0.0.1",
            "--port": "5000",
            "--window": str(run.window),
            "--payload": str(run.payload_bytes),
            "--data-error": "0.0",
            "--data-delay": "0.0",
            "--ack-error": "0.0",
            "--ack-delay": "0.3",
            "--seed": str(run.seed),
        }
        if pairs != expected:
            raise MatrixError(f"sender arguments {pairs} != {expected}")

    suite.case("sender arguments carry the run's parameters", sender_command_line_is_correct)

    def metrics_row_parses() -> None:
        row = "4096,10752,90,78,90,78,0,0,1309,46,46,10,0.380952,3129.106188,1.000000"
        parsed = runner.parse_metrics_row(row + "\n")
        if len(parsed) != len(runner.METRICS_COLUMNS):
            raise MatrixError(f"parsed {len(parsed)} columns")
        if parsed["unique_payload_bytes"] != 4096.0:
            raise MatrixError(f"unique_payload_bytes is {parsed['unique_payload_bytes']}")
        if parsed["efficiency"] != 0.380952:
            raise MatrixError(f"efficiency is {parsed['efficiency']}")

    suite.case("a valid metrics row parses into every column", metrics_row_parses)

    malformed = [
        ("empty output", ""),
        ("two rows", "1,2\n3,4\n"),
        ("too few columns", "4096,10752,90\n"),
        ("too many columns", ",".join(["1"] * 16) + "\n"),
        ("non-numeric value", ",".join(["1"] * 14 + ["nope"]) + "\n"),
    ]
    for name, text in malformed:
        try:
            runner.parse_metrics_row(text)
        except runner.MetricsError:
            suite.check(f"parser rejects {name}", True)
        except Exception as error:  # noqa: BLE001 - wrong exception type is a failure
            suite.check(f"parser rejects {name}", False, f"raised {error!r}")
        else:
            suite.check(f"parser rejects {name}", False, "no error raised")

    def experiment_columns_are_metadata_plus_metrics() -> None:
        columns = runner.EXPERIMENT_COLUMNS
        if columns[: len(runner.METADATA_COLUMNS)] != runner.METADATA_COLUMNS:
            raise MatrixError("metadata columns do not come first")
        if columns[len(runner.METADATA_COLUMNS) :] != runner.METRICS_COLUMNS:
            raise MatrixError("metrics columns do not follow the metadata")
        for required in ("protocol", "fcs", "impairment", "probability", "seed"):
            if required not in columns:
                raise MatrixError(f"missing required metadata column {required}")

    suite.case(
        "the CSV schema is matrix metadata followed by the sender's own columns",
        experiment_columns_are_metadata_plus_metrics,
    )

    def result_row_matches_the_schema() -> None:
        run = matrix[0]
        metrics = {name: float(index) for index, name in enumerate(runner.METRICS_COLUMNS)}
        row = runner.result_row(run, metrics)
        if len(row) != len(runner.EXPERIMENT_COLUMNS):
            raise MatrixError(f"row has {len(row)} fields for {len(runner.EXPERIMENT_COLUMNS)}")
        record = dict(zip(runner.EXPERIMENT_COLUMNS, row))
        if record["protocol"] != run.protocol or record["impairment"] != run.impairment:
            raise MatrixError(f"metadata not carried through: {record}")

    suite.case("a result row matches the declared schema", result_row_matches_the_schema)

    if suite.failures != 0:
        print(f"FAIL: {suite.failures} experiment-matrix case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
