"""SVG generation checks for ``tools/plot_results.py``.

Plots are generated from a small synthetic result set whose values are known
exactly, so every plotted point can be compared back to the CSV it came from.
The cases require well-formed XML, a complete figure set, points that match the
data, and a refusal to plot a result set that fails validation.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CPP_DIR / "tools"))
sys.path.insert(0, str(CPP_DIR / "tests"))

import plot_results as plotter  # noqa: E402 - path is configured above
import run_experiments as runner  # noqa: E402 - path is configured above
import validate_results as validator  # noqa: E402 - path is configured above
from test_validate_results import synthetic_rows, write_csv  # noqa: E402

PLOTTER = CPP_DIR / "tools" / "plot_results.py"
SVG_NAMESPACE = "http://www.w3.org/2000/svg"

TOOL_TIMEOUT_SECONDS = 60


class PlotError(AssertionError):
    """Raised when plot output is missing, malformed, or wrong."""


def run_plotter(input_path: Path, output_directory: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            str(PLOTTER),
            "--input",
            str(input_path),
            "--output-directory",
            str(output_directory),
        ],
        capture_output=True,
        text=True,
        timeout=TOOL_TIMEOUT_SECONDS,
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
    if not PLOTTER.exists():
        print(f"FAIL: missing tool {PLOTTER}", file=sys.stderr)
        return 1

    suite = Suite()
    rows = synthetic_rows()
    records = [dict(zip(runner.EXPERIMENT_COLUMNS, row)) for row in rows]

    with tempfile.TemporaryDirectory() as raw_directory:
        directory = Path(raw_directory)
        source = directory / "experiments.csv"
        write_csv(source, rows)
        plots = directory / "plots"

        result = run_plotter(source, plots)
        suite.check(
            "the plotter accepts a validated result set",
            result.returncode == 0,
            f"exit {result.returncode}: {result.stdout}{result.stderr}",
        )

        expected_names = {
            f"{metric}_vs_probability_{impairment}.svg"
            for metric in plotter.PLOTTED_METRICS
            for impairment in runner.IMPAIRMENTS
        }

        def every_figure_exists() -> None:
            produced = {path.name for path in plots.glob("*.svg")}
            if produced != expected_names:
                raise PlotError(
                    f"missing {sorted(expected_names - produced)}, "
                    f"unexpected {sorted(produced - expected_names)}"
                )

        suite.case(
            "one figure per metric and impairment dimension exists", every_figure_exists
        )

        def every_figure_is_well_formed_svg() -> None:
            for path in sorted(plots.glob("*.svg")):
                try:
                    root = ElementTree.parse(path).getroot()
                except ElementTree.ParseError as error:
                    raise PlotError(f"{path.name} is not well-formed XML: {error}") from error
                if root.tag != f"{{{SVG_NAMESPACE}}}svg":
                    raise PlotError(f"{path.name} root element is {root.tag}")
                for required in ("viewBox", "width", "height"):
                    if required not in root.attrib:
                        raise PlotError(f"{path.name} has no {required}")
                if root.find(f"{{{SVG_NAMESPACE}}}title") is None:
                    raise PlotError(f"{path.name} has no <title>")
                if root.find(f"{{{SVG_NAMESPACE}}}desc") is None:
                    raise PlotError(f"{path.name} has no <desc>")

        suite.case("every figure is well-formed SVG", every_figure_is_well_formed_svg)

        def points_match_the_source_data() -> None:
            for metric in plotter.PLOTTED_METRICS:
                for impairment in runner.IMPAIRMENTS:
                    path = plots / f"{metric}_vs_probability_{impairment}.svg"
                    root = ElementTree.parse(path).getroot()
                    plotted = {}
                    for point in root.iter(f"{{{SVG_NAMESPACE}}}circle"):
                        run_id = point.get("data-run")
                        if run_id is None:
                            continue
                        plotted[run_id] = point.get("data-value")

                    expected = {}
                    for record in records:
                        if record["impairment"] not in (impairment, "none"):
                            continue
                        expected[record["run_id"]] = record[metric]

                    if plotted != expected:
                        missing = set(expected) - set(plotted)
                        wrong = {
                            key: (plotted[key], expected[key])
                            for key in expected.keys() & plotted.keys()
                            if plotted[key] != expected[key]
                        }
                        raise PlotError(
                            f"{path.name}: missing {sorted(missing)}, wrong {wrong}"
                        )

        suite.case(
            "every plotted point carries its exact source value",
            points_match_the_source_data,
        )

        def each_figure_covers_all_three_protocols() -> None:
            for path in sorted(plots.glob("*.svg")):
                text = path.read_text(encoding="utf-8")
                for protocol in runner.PROTOCOLS:
                    if protocol not in text:
                        raise PlotError(f"{path.name} has no {protocol} series")

        suite.case(
            "every figure compares all three protocols",
            each_figure_covers_all_three_protocols,
        )

        def baseline_is_the_leftmost_point() -> None:
            path = plots / f"efficiency_vs_probability_{runner.IMPAIRMENTS[0]}.svg"
            root = ElementTree.parse(path).getroot()
            baselines = [
                point
                for point in root.iter(f"{{{SVG_NAMESPACE}}}circle")
                if (point.get("data-run") or "").endswith("__none")
            ]
            if len(baselines) != len(runner.PROTOCOLS):
                raise PlotError(f"expected one baseline point per protocol, got {len(baselines)}")
            for point in baselines:
                if point.get("data-probability") != "0.0":
                    raise PlotError(f"baseline point sits at {point.get('data-probability')}")

        suite.case(
            "the unimpaired baseline anchors every figure at probability 0.0",
            baseline_is_the_leftmost_point,
        )

        def efficiency_figures_state_the_definition() -> None:
            for impairment in runner.IMPAIRMENTS:
                path = plots / f"efficiency_vs_probability_{impairment}.svg"
                text = path.read_text(encoding="utf-8")
                if "unique_payload_bytes / transmitted_frame_bytes" not in text:
                    raise PlotError(f"{path.name} does not state the efficiency definition")

        suite.case(
            "efficiency figures carry the project's efficiency definition",
            efficiency_figures_state_the_definition,
        )

        def output_is_deterministic() -> None:
            first = {path.name: path.read_bytes() for path in sorted(plots.glob("*.svg"))}
            again = directory / "plots_again"
            if run_plotter(source, again).returncode != 0:
                raise PlotError("the second plot run failed")
            second = {path.name: path.read_bytes() for path in sorted(again.glob("*.svg"))}
            if first != second:
                differing = [name for name in first if first[name] != second.get(name)]
                raise PlotError(f"figures differ between runs: {differing}")

        suite.case("plot output is byte-identical between runs", output_is_deterministic)

        def refuses_invalid_results() -> None:
            broken = [list(row) for row in rows]
            index = runner.EXPERIMENT_COLUMNS.index("completion_ms")
            broken[0][index] = "0"
            path = directory / "broken.csv"
            write_csv(path, broken)
            result = run_plotter(path, directory / "should_not_exist")
            if result.returncode == 0:
                raise PlotError("the plotter charted a result set with a zero denominator")
            if broken[0][0] not in result.stdout + result.stderr:
                raise PlotError("the plotter did not name the offending run")

        suite.case(
            "the plotter refuses a result set that fails validation",
            refuses_invalid_results,
        )

        def refuses_a_missing_file() -> None:
            result = run_plotter(directory / "absent.csv", directory / "nowhere")
            if result.returncode == 0:
                raise PlotError("a missing results file was accepted")

        suite.case("the plotter refuses a missing results file", refuses_a_missing_file)

        def definition_comes_from_the_validator() -> None:
            if plotter.EFFICIENCY_DEFINITION is not validator.EFFICIENCY_DEFINITION:
                raise PlotError("the plotter restates the efficiency definition")

        suite.case(
            "the plotter reuses the single efficiency definition",
            definition_comes_from_the_validator,
        )

    if suite.failures != 0:
        print(f"FAIL: {suite.failures} plot case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
