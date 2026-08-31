#!/usr/bin/env python3
"""Renders ``report/report.md`` from validated experiment results.

The narrative -- protocol descriptions, the wire contract, the methodology, the
validation rules -- lives in ``report/report_template.md``. Everything that is
derived from measurements is substituted in here, so no number in the report can
drift from ``results/experiments.csv``.

The results are validated first. A result set that fails an identity is refused
outright: the report is evidence, and reporting on data that does not hold
together would be worse than producing nothing.

Nothing time-dependent or machine-dependent is written into the output. Every
evidence reference is a path relative to the report itself, so the report is
reproducible from committed files and renders identically on any machine.

Usage:
    uv run python C++/tools/generate_report.py [--input <csv>]
                  [--template <md>] [--output <md>]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CPP_DIR / "tools"))

from plot_results import (  # noqa: E402 - path is configured above
    IMPAIRMENT_TITLES,
    PLOTTED_METRICS,
)
from run_experiments import (  # noqa: E402 - path is configured above
    IMPAIRMENTS,
    PROBABILITIES,
    PROTOCOLS,
)
from validate_results import (  # noqa: E402 - path is configured above
    EFFICIENCY_DEFINITION,
    ResultsError,
    load_rows,
    validate,
)

DEFAULT_INPUT = CPP_DIR / "results" / "experiments.csv"
DEFAULT_TEMPLATE = CPP_DIR / "report" / "report_template.md"
DEFAULT_OUTPUT = CPP_DIR / "report" / "report.md"

#: Paths written into the report, relative to report/ so they survive a move.
RESULTS_REFERENCE = "../results/experiments.csv"
PLOTS_REFERENCE = "../results/plots"

#: Every placeholder the template may use. The renderer fails if the template
#: still contains an unsubstituted one.
PLACEHOLDERS = [
    "MATRIX_SUMMARY",
    "MATRIX_PARAMETERS",
    "EFFICIENCY_DEFINITION",
    "FLAGGED_RUNS",
    "BASELINE_TABLE",
    "FIGURES",
    "OBSERVATIONS",
    "RESULTS_TABLE",
]

PROTOCOL_TITLES = {
    "stop-and-wait": "Stop-and-Wait",
    "go-back-n": "Go-Back-N",
    "selective-repeat": "Selective Repeat",
}

#: Columns shown in the per-run results table, in order.
TABLE_COLUMNS = [
    "run_id",
    "impairment",
    "probability",
    "completion_ms",
    "goodput_bytes_per_second",
    "efficiency",
    "original_transmissions",
    "retransmissions",
    "acks",
    "timeouts",
    "duplicates",
    "out_of_order",
    "mean_rtt_ms",
    "current_timeout_ms",
]


class ReportError(Exception):
    """Raised when the report cannot be rendered."""


def markdown_table(header: list[str], rows: list[list[str]]) -> str:
    """Renders a GitHub-flavoured Markdown table."""
    lines = [
        "| " + " | ".join(header) + " |",
        "| " + " | ".join("---" for _ in header) + " |",
    ]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(lines)


def find(rows: list[dict[str, str]], protocol: str, impairment: str, probability: str):
    for row in rows:
        if (
            row["protocol"] == protocol
            and row["impairment"] == impairment
            and row["probability"] == probability
        ):
            return row
    raise ReportError(f"no run for {protocol} {impairment} at {probability}")


def render_matrix_summary(rows: list[dict[str, str]]) -> str:
    baselines = sum(1 for row in rows if row["impairment"] == "none")
    return (
        f"The matrix contains **{len(rows)} runs**: {baselines} unimpaired baseline "
        f"runs, one per protocol, plus {len(PROTOCOLS)} protocols x "
        f"{len(IMPAIRMENTS)} impairment paths x {len(PROBABILITIES)} probability "
        f"levels ({', '.join(f'{level:.1f}' for level in PROBABILITIES)}). Every run "
        "transferred the whole input file byte for byte; the runner aborts the "
        "matrix if a reconstructed file differs from its input, so every row below "
        "describes a transfer that actually succeeded."
    )


def render_matrix_parameters(rows: list[dict[str, str]]) -> str:
    sample = rows[0]
    windows = ", ".join(
        f"{PROTOCOL_TITLES[protocol]} {next(r['window'] for r in rows if r['protocol'] == protocol)}"
        for protocol in PROTOCOLS
    )
    payload = int(sample["payload_bytes"])
    input_bytes = int(sample["input_bytes"])
    frames = -(-input_bytes // payload)
    parameters = [
        [
            "Input file",
            f"`test_data/input.bin`, {input_bytes} bytes",
            "Deterministic fixture; 4096 is not a multiple of 46, so the final "
            "frame is short and the short-final-frame path is exercised.",
        ],
        [
            "FCS scheme",
            f"`{sample['fcs']}`",
            "CRC-16 (`0x8005`) has a 2-byte FCS and far stronger burst detection "
            "than Checksum-16 or CRC-8/10, without CRC-32's 4-byte overhead "
            "skewing the efficiency ratio. The end-to-end suite separately proves "
            "every protocol is byte-exact under all five schemes.",
        ],
        [
            "Payload size",
            f"{payload} bytes",
            f"Safe for every FCS scheme and the professor's suggested size; it "
            f"splits the input into {frames} frames.",
        ],
        [
            "Window size",
            windows,
            "Inside both protocol limits (Go-Back-N <= 255, Selective Repeat "
            f"<= 128) and small relative to the {frames}-frame transfer, so the "
            "window fills and drains many times. Stop-and-Wait is one outstanding "
            "frame by definition and the sender forces its window to 1.",
        ],
        [
            "Seed",
            f"`{sample['seed']}`",
            "One seed for every run, so each protocol meets the same channel "
            "decision sequence. Frame selection and error-position selection are "
            "seeded independently inside the channel.",
        ],
    ]
    return markdown_table(["Parameter", "Value", "Why"], parameters)


def render_flagged_runs(flags: list[str]) -> str:
    if not flags:
        return (
            "No run in this result set has an unmeasurable mean RTT; every run "
            "produced at least one unambiguous RTT sample."
        )
    names = [flag.split(":", 1)[0] for flag in flags]
    lines = [
        f"In this result set {len(names)} run(s) produced no unambiguous RTT "
        "sample at all, so their mean RTT is **not measurable** and is excluded "
        "from RTT interpretation:",
        "",
    ]
    lines.extend(f"- `{name}`" for name in names)
    return "\n".join(lines)


def render_baseline_table(rows: list[dict[str, str]]) -> str:
    table_rows = []
    for protocol in PROTOCOLS:
        row = find(rows, protocol, "none", "0.0")
        table_rows.append(
            [
                PROTOCOL_TITLES[protocol],
                row["completion_ms"],
                f"{float(row['goodput_bytes_per_second']):,.0f}",
                row["efficiency"],
                row["original_transmissions"],
                row["retransmissions"],
                row["acks"],
                row["mean_rtt_ms"],
            ]
        )
    table = markdown_table(
        [
            "Protocol",
            "Completion (ms)",
            "Goodput (B/s)",
            "Efficiency",
            "Original transmissions",
            "Retransmissions",
            "ACKs",
            "Mean RTT (ms)",
        ],
        table_rows,
    )
    return (
        f"{table}\n\n"
        "With no impairment every protocol delivers the file with zero "
        "retransmissions and zero timeouts, and all three reach the same "
        "efficiency, because efficiency then measures nothing but framing "
        "overhead. What separates them is completion time: the two windowed "
        "protocols keep several frames in flight per round, while Stop-and-Wait "
        "pays a full round trip for every single frame."
    )


def render_figures() -> str:
    sections: list[str] = []
    for impairment in IMPAIRMENTS:
        sections.append(f"#### {IMPAIRMENT_TITLES[impairment]}")
        sections.append("")
        for metric, (label, _scale) in PLOTTED_METRICS.items():
            name = f"{metric}_vs_probability_{impairment}.svg"
            caption = f"{label.split(' (')[0]} versus {IMPAIRMENT_TITLES[impairment]}"
            sections.append(f"![{caption}]({PLOTS_REFERENCE}/{name})")
            sections.append("")
            sections.append(f"*{caption}. Source: [`{name}`]({PLOTS_REFERENCE}/{name}).*")
            sections.append("")
    return "\n".join(sections).rstrip()


def identical_series(
    rows: list[dict[str, str]], impairment: str, metric: str, first: str, second: str
) -> bool:
    """Reports whether two protocols produced an identical series for a metric."""
    return all(
        find(rows, first, impairment, f"{probability:.1f}")[metric]
        == find(rows, second, impairment, f"{probability:.1f}")[metric]
        for probability in PROBABILITIES
    )


def names_of(protocols: list[str]) -> str:
    """Joins protocol titles into readable English."""
    titles = [PROTOCOL_TITLES[protocol] for protocol in protocols]
    if len(titles) == 1:
        return titles[0]
    return ", ".join(titles[:-1]) + " and " + titles[-1]


def render_observations(rows: list[dict[str, str]]) -> str:
    sections: list[str] = []

    for impairment in IMPAIRMENTS:
        sections.append(f"**{IMPAIRMENT_TITLES[impairment]}.**")
        sections.append("")
        worst = [find(rows, protocol, impairment, "0.5") for protocol in PROTOCOLS]
        table = markdown_table(
            [
                "Protocol",
                "Completion (ms)",
                "Goodput (B/s)",
                "Efficiency",
                "Retransmissions",
                "Timeouts",
            ],
            [
                [
                    PROTOCOL_TITLES[row["protocol"]],
                    row["completion_ms"],
                    f"{float(row['goodput_bytes_per_second']):,.0f}",
                    row["efficiency"],
                    row["retransmissions"],
                    row["timeouts"],
                ]
                for row in worst
            ],
        )
        sections.append(f"At the hardest level tested, probability 0.5:")
        sections.append("")
        sections.append(table)
        sections.append("")

        best_time = min(int(row["completion_ms"]) for row in worst)
        fastest = [row for row in worst if int(row["completion_ms"]) == best_time]
        best_efficiency = max(row["efficiency"] for row in worst)
        leanest = [row for row in worst if row["efficiency"] == best_efficiency]
        slowdown = best_time / int(
            find(rows, fastest[0]["protocol"], "none", "0.0")["completion_ms"]
        )
        sections.append(
            f"{names_of([row['protocol'] for row in fastest])} "
            f"{'finish' if len(fastest) > 1 else 'finishes'} fastest "
            f"({best_time} ms, {slowdown:.1f}x the unimpaired baseline), and "
            f"{names_of([row['protocol'] for row in leanest])} "
            f"{'reach' if len(leanest) > 1 else 'reaches'} the highest efficiency "
            f"({best_efficiency})."
        )

        for metric in PLOTTED_METRICS:
            pairs = [
                (first, second)
                for index, first in enumerate(PROTOCOLS)
                for second in PROTOCOLS[index + 1 :]
                if identical_series(rows, impairment, metric, first, second)
            ]
            for first, second in pairs:
                sections.append(
                    f"{PROTOCOL_TITLES[first]} and {PROTOCOL_TITLES[second]} "
                    f"produce *identical* `{metric}` at every level on this path, "
                    "so their curves coincide exactly in that figure; the figures "
                    "separate coincident series by dash pattern and marker shape "
                    "rather than by colour alone."
                )

        never_retransmits = [
            row["protocol"]
            for row in worst
            if all(
                find(rows, row["protocol"], impairment, f"{level:.1f}")["retransmissions"]
                == "0"
                for level in PROBABILITIES
            )
        ]
        if never_retransmits:
            sections.append(
                f"{names_of(never_retransmits)} never retransmits on this path at "
                "any level tested."
            )
        sections.append("")

    sections.append("**Reading the comparison as a whole.**")
    sections.append("")
    sections.append(
        "Impairment on the DATA path and impairment on the ACK path are not "
        "symmetric, and the asymmetry follows directly from how each protocol "
        "acknowledges. Go-Back-N's acknowledgments are cumulative, so a later "
        "acknowledgment subsumes every earlier one it passes: with a window of "
        "frames acknowledged per round, losing individual acknowledgments costs "
        "it almost nothing. Stop-and-Wait has exactly one acknowledgment in "
        "flight and Selective Repeat needs each frame acknowledged "
        "individually, so both must recover from every acknowledgment that is "
        "corrupted or delayed."
    )
    sections.append("")
    sections.append(
        "On the DATA path the ordering reverses. Go-Back-N's whole-window "
        "retransmission turns each lost frame into a burst of resends, and the "
        "cost compounds as the probability rises, while Selective Repeat "
        "retransmits only what was actually missed and tracks Stop-and-Wait's "
        "retransmission count exactly, at a fraction of its completion time."
    )
    sections.append("")
    sections.append(
        "A corrupted DATA frame and an excessively delayed DATA frame are "
        "indistinguishable in these metrics, and the two sweeps produce "
        "identical rows. That is expected rather than a defect: the receiver "
        "discards a frame that fails its FCS check without acknowledging it, "
        "which is exactly what it does with a frame that never arrived, and the "
        "sender counts a frame's wire bytes when it transmits the frame, before "
        "the channel decides its fate. With the impairment probability applied "
        "to the same channel decision stream in both sweeps, the same "
        "transmissions are affected in both."
    )

    return "\n".join(sections).rstrip()


def render_results_table(rows: list[dict[str, str]]) -> str:
    sections: list[str] = []
    for protocol in PROTOCOLS:
        sections.append(f"#### {PROTOCOL_TITLES[protocol]}")
        sections.append("")
        sections.append(
            markdown_table(
                [column.replace("_", " ") for column in TABLE_COLUMNS],
                [
                    [row[column] for column in TABLE_COLUMNS]
                    for row in rows
                    if row["protocol"] == protocol
                ],
            )
        )
        sections.append("")
    return "\n".join(sections).rstrip()


def render(rows: list[dict[str, str]], flags: list[str], template: str) -> str:
    """Substitutes every declared placeholder into the template."""
    substitutions = {
        "MATRIX_SUMMARY": render_matrix_summary(rows),
        "MATRIX_PARAMETERS": render_matrix_parameters(rows),
        "EFFICIENCY_DEFINITION": f"> {EFFICIENCY_DEFINITION}",
        "FLAGGED_RUNS": render_flagged_runs(flags),
        "BASELINE_TABLE": render_baseline_table(rows),
        "FIGURES": render_figures(),
        "OBSERVATIONS": render_observations(rows),
        "RESULTS_TABLE": render_results_table(rows),
    }
    if sorted(substitutions) != sorted(PLACEHOLDERS):
        raise ReportError("PLACEHOLDERS does not match the rendered substitutions")

    document = template
    for name, value in substitutions.items():
        document = document.replace("{{" + name + "}}", value)

    if "{{" in document:
        remaining = {
            fragment.split("}}")[0]
            for fragment in document.split("{{")[1:]
            if "}}" in fragment
        }
        raise ReportError(f"template has unsubstituted placeholders: {sorted(remaining)}")
    return document


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="generate_report.py",
        description="Render report/report.md from validated experiment results.",
        epilog=EFFICIENCY_DEFINITION,
    )
    parser.add_argument("--input", default=str(DEFAULT_INPUT))
    parser.add_argument("--template", default=str(DEFAULT_TEMPLATE))
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)

    template_path = Path(arguments.template)
    if not template_path.exists():
        print(f"generate_report: missing template {template_path}", file=sys.stderr)
        return 1

    try:
        rows = load_rows(Path(arguments.input))
    except ResultsError as error:
        print(f"generate_report: {error}", file=sys.stderr)
        return 1

    violations, flags = validate(rows)
    if violations:
        for violation in violations:
            print(f"VIOLATION: {violation}", file=sys.stderr)
        print(
            "generate_report: refusing to report on results that fail validation",
            file=sys.stderr,
        )
        return 1

    try:
        document = render(rows, flags, template_path.read_text(encoding="utf-8"))
    except ReportError as error:
        print(f"generate_report: {error}", file=sys.stderr)
        return 1

    output = Path(arguments.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(document, encoding="utf-8")

    print(f"{output}: {len(rows)} runs reported")
    return 0


if __name__ == "__main__":
    sys.exit(main())
