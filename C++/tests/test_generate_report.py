"""Report-rendering checks for ``tools/generate_report.py``.

The report is evidence, so these cases require it to be reproducible from
committed files: rendered from a template, filled only from a validated results
file, referring to the CSV and the figures by relative path, and containing no
machine-specific absolute path or timestamp that would make two runs differ.
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CPP_DIR / "tools"))
sys.path.insert(0, str(CPP_DIR / "tests"))

import generate_report as reporter  # noqa: E402 - path is configured above
import plot_results as plotter  # noqa: E402 - path is configured above
import run_experiments as runner  # noqa: E402 - path is configured above
import validate_results as validator  # noqa: E402 - path is configured above
from test_validate_results import synthetic_rows, write_csv  # noqa: E402

REPORTER = CPP_DIR / "tools" / "generate_report.py"
TEMPLATE = CPP_DIR / "report" / "report_template.md"

TOOL_TIMEOUT_SECONDS = 60


class ReportError(AssertionError):
    """Raised when the rendered report is missing, wrong, or unreproducible."""


def run_reporter(
    source: Path, output: Path, *, template: Path | None = None
) -> subprocess.CompletedProcess:
    arguments = [
        sys.executable,
        str(REPORTER),
        "--input",
        str(source),
        "--output",
        str(output),
    ]
    if template is not None:
        arguments += ["--template", str(template)]
    return subprocess.run(
        arguments, capture_output=True, text=True, timeout=TOOL_TIMEOUT_SECONDS
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
    for required in (REPORTER, TEMPLATE):
        if not required.exists():
            print(f"FAIL: missing {required}", file=sys.stderr)
            return 1

    suite = Suite()
    rows = synthetic_rows()
    records = [dict(zip(runner.EXPERIMENT_COLUMNS, row)) for row in rows]

    with tempfile.TemporaryDirectory() as raw_directory:
        directory = Path(raw_directory)
        source = directory / "experiments.csv"
        write_csv(source, rows)
        output = directory / "report.md"

        result = run_reporter(source, output)
        suite.check(
            "the reporter renders from a validated result set",
            result.returncode == 0 and output.exists(),
            f"exit {result.returncode}: {result.stdout}{result.stderr}",
        )
        report = output.read_text(encoding="utf-8") if output.exists() else ""

        def every_placeholder_is_substituted() -> None:
            leftover = re.findall(r"\{\{[A-Z_]+\}\}", report)
            if leftover:
                raise ReportError(f"unsubstituted placeholders {sorted(set(leftover))}")
            if not reporter.PLACEHOLDERS:
                raise ReportError("the reporter declares no placeholders")
            template = TEMPLATE.read_text(encoding="utf-8")
            for name in reporter.PLACEHOLDERS:
                if "{{" + name + "}}" not in template:
                    raise ReportError(f"the template never uses {{{{{name}}}}}")

        suite.case(
            "every template placeholder is declared and substituted",
            every_placeholder_is_substituted,
        )

        def states_the_efficiency_definition() -> None:
            if validator.EFFICIENCY_DEFINITION not in report:
                raise ReportError("the report does not state the efficiency definition")
            if reporter.EFFICIENCY_DEFINITION is not validator.EFFICIENCY_DEFINITION:
                raise ReportError("the reporter restates the efficiency definition")

        suite.case(
            "the report carries the single efficiency definition",
            states_the_efficiency_definition,
        )

        def references_evidence_by_relative_path() -> None:
            if "../results/experiments.csv" not in report:
                raise ReportError("the report does not reference the results CSV")
            for metric in plotter.PLOTTED_METRICS:
                for impairment in runner.IMPAIRMENTS:
                    reference = f"../results/plots/{metric}_vs_probability_{impairment}.svg"
                    if reference not in report:
                        raise ReportError(f"the report does not embed {reference}")

        suite.case(
            "the report references the CSV and every figure by relative path",
            references_evidence_by_relative_path,
        )

        def contains_no_machine_specific_path() -> None:
            for pattern in (str(CPP_DIR), str(directory), "/home/", "/tmp/"):
                if pattern in report:
                    raise ReportError(f"the report embeds the absolute path {pattern!r}")

        suite.case(
            "the report contains no absolute or machine-specific path",
            contains_no_machine_specific_path,
        )

        def tabulates_every_run() -> None:
            for record in records:
                if record["run_id"] not in report:
                    raise ReportError(f"run {record['run_id']} is missing from the report")

        suite.case("the results table covers every run", tabulates_every_run)

        def describes_protocols_and_wire_contract() -> None:
            for topic in (
                "Stop-and-Wait",
                "Go-Back-N",
                "Selective Repeat",
                "sequence number",
                "FCS",
            ):
                if topic.lower() not in report.lower():
                    raise ReportError(f"the report never discusses {topic!r}")

        suite.case(
            "the report describes the protocols and the wire contract",
            describes_protocols_and_wire_contract,
        )

        def documents_the_methodology() -> None:
            for topic in ("one-factor-at-a-time", "seed", "crc16", "46"):
                if topic.lower() not in report.lower():
                    raise ReportError(f"the methodology never mentions {topic!r}")

        suite.case("the report documents the experiment methodology", documents_the_methodology)

        def reports_flagged_runs() -> None:
            flagged_rows = [list(row) for row in rows]
            index = runner.EXPERIMENT_COLUMNS.index("rtt_sample_count")
            total_index = runner.EXPERIMENT_COLUMNS.index("total_rtt_ms")
            mean_index = runner.EXPERIMENT_COLUMNS.index("mean_rtt_ms")
            target = "go-back-n__data-error__0.5"
            for row in flagged_rows:
                if row[0] == target:
                    row[index] = "0"
                    row[total_index] = "0"
                    row[mean_index] = "0.000000"
            flagged_source = directory / "flagged.csv"
            flagged_output = directory / "flagged_report.md"
            write_csv(flagged_source, flagged_rows)
            if run_reporter(flagged_source, flagged_output).returncode != 0:
                raise ReportError("a flagged but valid result set was refused")
            text = flagged_output.read_text(encoding="utf-8")
            if target not in text or "not measurable" not in text.lower():
                raise ReportError("the report does not disclose the unmeasurable RTT run")

        suite.case(
            "the report discloses runs whose mean RTT is not measurable",
            reports_flagged_runs,
        )

        def discloses_every_coincident_impairment_path() -> None:
            # In this fixture the metrics depend only on probability, so all
            # four sweeps coincide and all six pairs must be disclosed -- not
            # just the DATA pair.
            pairs = reporter.coincident_paths(records)
            expected = [
                (first, second)
                for index, first in enumerate(runner.IMPAIRMENTS)
                for second in runner.IMPAIRMENTS[index + 1 :]
            ]
            if pairs != expected:
                raise ReportError(f"detected {pairs}, expected every pair {expected}")
            for first, second in expected:
                if f"`{first}` and `{second}`" not in report:
                    raise ReportError(f"the report never discloses {first}/{second}")
            if "duplicates of the" not in report:
                raise ReportError("the report does not say the figures are duplicates")

        suite.case(
            "the report discloses every coincident impairment-path pair",
            discloses_every_coincident_impairment_path,
        )

        def discloses_both_data_and_ack_coincidences() -> None:
            # The DATA and the ACK coincidence have different mechanisms, and
            # disclosing only one of them was the review finding this covers.
            for path in ("data-error", "data-delay", "ack-error", "ack-delay"):
                if f"`{path}`" not in report:
                    raise ReportError(f"{path} is never named in the disclosure")
            if "fails its FCS check without" not in report:
                raise ReportError("the DATA-path mechanism is not explained")
            if "complement" not in report:
                raise ReportError("the ACK-path mechanism is not explained")

        suite.case(
            "both the DATA-path and the ACK-path coincidence are explained",
            discloses_both_data_and_ack_coincidences,
        )

        def claims_no_coincidence_that_the_data_does_not_show() -> None:
            # Push data-delay off the shared curve; data-error/data-delay must
            # then no longer be reported as coincident, while ack-error and
            # ack-delay still must be.
            distinct = synthetic_rows(
                extra=lambda run: 1 if run.impairment == "data-delay" else 0
            )
            distinct_records = [
                dict(zip(runner.EXPERIMENT_COLUMNS, row)) for row in distinct
            ]
            pairs = reporter.coincident_paths(distinct_records)
            if ("data-error", "data-delay") in pairs:
                raise ReportError("diverging sweeps were still reported as identical")
            if ("ack-error", "ack-delay") not in pairs:
                raise ReportError("the ACK pair stopped being detected")

            source = directory / "distinct.csv"
            target = directory / "distinct_report.md"
            write_csv(source, distinct)
            result = run_reporter(source, target)
            if result.returncode != 0:
                raise ReportError(f"a valid diverging result set was refused: {result.stderr}")
            text = target.read_text(encoding="utf-8")
            if "`data-error` and `data-delay`" in text:
                raise ReportError("the report claims a coincidence the data does not show")
            if "`ack-error` and `ack-delay`" not in text:
                raise ReportError("the report dropped the ACK coincidence it should keep")

        suite.case(
            "the disclosure follows the data rather than a fixed string",
            claims_no_coincidence_that_the_data_does_not_show,
        )

        def render_path_coincidences_reports_true_absence() -> None:
            # Every prior coincidence test uses a fixture where at least one
            # pair coincides; this is the only one that exercises the
            # fallback branch where none of the six pairs do.
            offsets = {"data-error": 0, "data-delay": 1, "ack-error": 2, "ack-delay": 3}
            distinct = synthetic_rows(extra=lambda run: offsets.get(run.impairment, 0))
            distinct_records = [dict(zip(runner.EXPERIMENT_COLUMNS, row)) for row in distinct]
            pairs = reporter.coincident_paths(distinct_records)
            if pairs:
                raise ReportError(f"expected no coincident pairs, found {pairs}")

            source = directory / "no_coincidence.csv"
            target = directory / "no_coincidence_report.md"
            write_csv(source, distinct)
            result = run_reporter(source, target)
            if result.returncode != 0:
                raise ReportError(f"a valid non-coincident result set was refused: {result.stderr}")
            text = target.read_text(encoding="utf-8")
            if "distinct result set" not in text:
                raise ReportError("the report does not state that no sweeps coincide")
            if "pair(s) of impairment sweeps coincide exactly" in text:
                raise ReportError("the report claims a coincidence when none exists")

        suite.case(
            "the report states plainly when no impairment sweeps coincide",
            render_path_coincidences_reports_true_absence,
        )

        def completion_estimator_caveat_names_a_flagged_shown_run() -> None:
            shown = [
                {"run_id": "go-back-n__data-error__0.5"},
                {"run_id": "go-back-n__data-error__0.4"},
            ]
            flagged = reporter.flagged_run_ids(
                ["go-back-n__data-error__0.5: no unambiguous RTT sample"]
            )
            note = reporter.completion_estimator_caveat(shown, flagged)
            if (
                note is None
                or "go-back-n__data-error__0.5" not in note
                or "100 ms constructor default" not in note
            ):
                raise ReportError(f"the caveat does not name the flagged run: {note!r}")
            if "go-back-n__data-error__0.4" in note:
                raise ReportError("the caveat named a run that was not flagged")

        suite.case(
            "completion_estimator_caveat names a flagged run shown in the table",
            completion_estimator_caveat_names_a_flagged_shown_run,
        )

        def completion_estimator_caveat_is_silent_otherwise() -> None:
            shown = [{"run_id": "stop-and-wait__data-error__0.5"}]
            flagged = reporter.flagged_run_ids(
                ["go-back-n__data-error__0.5: no unambiguous RTT sample"]
            )
            if reporter.completion_estimator_caveat(shown, flagged) is not None:
                raise ReportError("the caveat fired for a run that was not flagged")
            if reporter.completion_estimator_caveat(shown, []) is not None:
                raise ReportError("the caveat fired with no flagged runs at all")

        suite.case(
            "completion_estimator_caveat is silent when nothing shown is flagged",
            completion_estimator_caveat_is_silent_otherwise,
        )

        def gbn_ack_path_free_selects_the_correct_clause() -> None:
            # The default fixture gives every protocol the same nonzero
            # retransmission count at every nonzero probability, so Go-Back-N's
            # ACK paths are not retransmission-free here -- the false branch.
            if reporter.gbn_ack_path_retransmission_free(records) is not False:
                raise ReportError("the default fixture should not be ACK-retransmission-free")
            if "rarely forces a retransmission" not in report:
                raise ReportError("the report does not render the non-free clause")

            free = synthetic_rows(
                extra=lambda run: -int(round(run.probability * 100))
                if run.protocol == "go-back-n" and run.impairment in ("ack-error", "ack-delay")
                else 0
            )
            free_records = [dict(zip(runner.EXPERIMENT_COLUMNS, row)) for row in free]
            if reporter.gbn_ack_path_retransmission_free(free_records) is not True:
                raise ReportError("forcing zero ACK-path retransmissions was not detected")

            source = directory / "gbn_ack_free.csv"
            target = directory / "gbn_ack_free_report.md"
            write_csv(source, free)
            result = run_reporter(source, target)
            if result.returncode != 0:
                raise ReportError(f"a valid retransmission-free result set was refused: {result.stderr}")
            if "zero retransmissions at every level tested" not in target.read_text(encoding="utf-8"):
                raise ReportError("the report does not render the retransmission-free clause")

        suite.case(
            "gbn_ack_path_retransmission_free selects the correct report clause",
            gbn_ack_path_free_selects_the_correct_clause,
        )

        def sr_matches_saw_selects_the_correct_clause() -> None:
            # The default fixture uses one retransmission formula for every
            # protocol, so Selective Repeat matches Stop-and-Wait exactly here
            # -- the true branch.
            if reporter.sr_matches_stop_and_wait_retransmissions(records, "data-error") is not True:
                raise ReportError("the default fixture should have SR match Stop-and-Wait exactly")
            if "matching Stop-and-Wait's retransmission count exactly" not in report:
                raise ReportError("the report does not render the exact-match clause")

            diverging = synthetic_rows(
                extra=lambda run: 5
                if run.protocol == "selective-repeat" and run.impairment == "data-error"
                else 0
            )
            diverging_records = [dict(zip(runner.EXPERIMENT_COLUMNS, row)) for row in diverging]
            if (
                reporter.sr_matches_stop_and_wait_retransmissions(diverging_records, "data-error")
                is not False
            ):
                raise ReportError("diverging SR retransmissions were not detected")

            source = directory / "sr_diverges.csv"
            target = directory / "sr_diverges_report.md"
            write_csv(source, diverging)
            result = run_reporter(source, target)
            if result.returncode != 0:
                raise ReportError(f"a valid diverging result set was refused: {result.stderr}")
            if "landing close to Stop-and-Wait's retransmission count" not in target.read_text(
                encoding="utf-8"
            ):
                raise ReportError("the report does not render the near-match clause")

        suite.case(
            "sr_matches_stop_and_wait_retransmissions selects the correct report clause",
            sr_matches_saw_selects_the_correct_clause,
        )

        def rendering_is_deterministic() -> None:
            again = directory / "report_again.md"
            if run_reporter(source, again).returncode != 0:
                raise ReportError("the second render failed")
            if again.read_text(encoding="utf-8") != report:
                raise ReportError("two renders of the same results differ")

        suite.case("rendering the same results twice is identical", rendering_is_deterministic)

        def refuses_invalid_results() -> None:
            broken = [list(row) for row in rows]
            index = runner.EXPERIMENT_COLUMNS.index("efficiency")
            broken[0][index] = "0.999999"
            path = directory / "broken.csv"
            write_csv(path, broken)
            target = directory / "never_written.md"
            result = run_reporter(path, target)
            if result.returncode == 0:
                raise ReportError("a result set that fails validation was reported on")
            if target.exists():
                raise ReportError("a refused render still wrote a report")

        suite.case(
            "the reporter refuses a result set that fails validation",
            refuses_invalid_results,
        )

        def refuses_a_missing_template() -> None:
            result = run_reporter(
                source, directory / "no_template.md", template=directory / "absent.md"
            )
            if result.returncode == 0:
                raise ReportError("a missing template was accepted")

        suite.case("the reporter refuses a missing template", refuses_a_missing_template)

        def refuses_a_missing_results_file() -> None:
            if run_reporter(directory / "absent.csv", directory / "x.md").returncode == 0:
                raise ReportError("a missing results file was accepted")

        suite.case(
            "the reporter refuses a missing results file", refuses_a_missing_results_file
        )

    if suite.failures != 0:
        print(f"FAIL: {suite.failures} report case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
