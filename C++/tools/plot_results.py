#!/usr/bin/env python3
"""Renders the experiment results as validated SVG figures.

The root Python environment declares no dependencies, so the charts are
hand-rolled SVG built by string templating. That is deliberate: a line chart
over 18 points is small enough to write exactly, the output is deterministic and
diffable, and every plotted point carries its own source value in a ``data-run``
/ ``data-value`` attribute pair so a reader -- or a test -- can check the picture
against ``results/experiments.csv`` without trusting the renderer.

The results file is validated before anything is drawn. A result set with a
broken identity or a zero denominator is refused rather than charted.

One figure is produced per metric per impairment dimension. Each figure plots
the three protocols against probability, anchored on the left at the shared
unimpaired baseline (probability 0.0), which is the same run for every
dimension.

Usage:
    uv run python C++/tools/plot_results.py [--input <csv>]
                  [--output-directory <dir>]
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path
from xml.sax.saxutils import escape, quoteattr

CPP_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CPP_DIR / "tools"))

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
DEFAULT_OUTPUT_DIRECTORY = CPP_DIR / "results" / "plots"

SVG_NAMESPACE = "http://www.w3.org/2000/svg"

#: Metric column -> (axis label, axis scale). A logarithmic axis is used where
#: the values span more than two decades, which completion time and goodput do
#: once Go-Back-N starts retransmitting whole windows; a linear axis there would
#: flatten every protocol except the worst one into the baseline.
PLOTTED_METRICS = {
    "completion_ms": ("Completion time (ms, logarithmic)", "log"),
    "goodput_bytes_per_second": ("Goodput (bytes/second, logarithmic)", "log"),
    "efficiency": ("Efficiency (delivered payload / transmitted frame bytes)", "linear"),
    "retransmissions": ("Retransmitted data frames", "linear"),
}

#: Human-readable names for the four independently swept impairment paths.
IMPAIRMENT_TITLES = {
    "data-error": "DATA-path bit corruption",
    "data-delay": "DATA-path excessive delay (loss)",
    "ack-error": "ACK-path bit corruption",
    "ack-delay": "ACK-path excessive delay (loss)",
}

#: Colour, label, dash pattern, marker radius, and marker fill per protocol,
#: stable across every figure. Two protocols often produce *identical* numbers
#: -- Stop-and-Wait and Selective Repeat retransmit exactly the same frames
#: under DATA-path impairment, and their efficiency curves coincide exactly --
#: so a colour alone would hide one series completely. Selective Repeat is
#: therefore drawn last as a wide open ring over a dotted line, which reads as a
#: halo around whichever filled marker it lands on rather than erasing it.
SERIES_STYLE = {
    "stop-and-wait": ("#1b6ca8", "Stop-and-Wait", "none", 4.5, True),
    "go-back-n": ("#c2410c", "Go-Back-N", "8 4", 4.0, True),
    "selective-repeat": ("#2f7d32", "Selective Repeat", "3 3", 7.5, False),
}

WIDTH = 760
HEIGHT = 460
MARGIN_LEFT = 96
MARGIN_RIGHT = 24
MARGIN_TOP = 64
MARGIN_BOTTOM = 108

PLOT_LEFT = MARGIN_LEFT
PLOT_RIGHT = WIDTH - MARGIN_RIGHT
PLOT_TOP = MARGIN_TOP
PLOT_BOTTOM = HEIGHT - MARGIN_BOTTOM

#: Probability positions on the x axis, baseline first.
X_VALUES = [0.0] + list(PROBABILITIES)


class PlotError(Exception):
    """Raised when the results cannot be plotted."""


def coordinate(value: float) -> str:
    """Formats a coordinate deterministically, without a trailing ``-0.0``."""
    rounded = round(value, 3)
    if rounded == 0:
        rounded = 0.0
    return f"{rounded:g}"


def nice_linear_ticks(maximum: float) -> list[float]:
    """Returns 0-anchored linear ticks covering ``maximum``."""
    if maximum <= 0:
        return [0.0, 1.0]
    exponent = math.floor(math.log10(maximum))
    for step_multiplier in (1.0, 2.0, 2.5, 5.0, 10.0):
        step = step_multiplier * (10.0**exponent) / 5.0
        if step > 0 and maximum / step <= 8:
            break
    ticks = []
    value = 0.0
    while value < maximum + step / 2:
        ticks.append(round(value, 10))
        value += step
    return ticks


def decade_ticks(minimum: float, maximum: float) -> list[float]:
    """Returns power-of-ten ticks spanning a logarithmic axis."""
    low = math.floor(math.log10(minimum))
    high = math.ceil(math.log10(maximum))
    if high <= low:
        high = low + 1
    return [10.0**exponent for exponent in range(int(low), int(high) + 1)]


def format_tick(value: float) -> str:
    if value >= 1000 or (value and abs(value) < 0.01):
        return f"{value:g}"
    if value == int(value):
        return str(int(value))
    return f"{value:g}"


class Axis:
    """Maps a metric value onto a y pixel, linearly or logarithmically."""

    def __init__(self, values: list[float], scale: str) -> None:
        self.scale = scale
        if scale == "log":
            positive = [value for value in values if value > 0]
            if not positive:
                raise PlotError("a logarithmic axis needs a positive value")
            self.ticks = decade_ticks(min(positive), max(positive))
            self.low = math.log10(self.ticks[0])
            self.high = math.log10(self.ticks[-1])
        else:
            self.ticks = nice_linear_ticks(max(values + [0.0]))
            self.low = 0.0
            self.high = float(self.ticks[-1]) or 1.0

    def position(self, value: float) -> float:
        if self.scale == "log":
            clamped = max(value, 10.0**self.low)
            fraction = (math.log10(clamped) - self.low) / (self.high - self.low)
        else:
            span = self.high - self.low
            fraction = (value - self.low) / span if span else 0.0
        fraction = min(max(fraction, 0.0), 1.0)
        return PLOT_BOTTOM - fraction * (PLOT_BOTTOM - PLOT_TOP)


def x_position(probability: float) -> float:
    index = X_VALUES.index(round(probability, 3))
    step = (PLOT_RIGHT - PLOT_LEFT) / (len(X_VALUES) - 1)
    return PLOT_LEFT + index * step


def series_for(
    rows: list[dict[str, str]], protocol: str, impairment: str, metric: str
) -> list[tuple[float, float, str, str]]:
    """Returns this protocol's (probability, value, run_id, raw text) points."""
    points: list[tuple[float, float, str, str]] = []
    for row in rows:
        if row["protocol"] != protocol:
            continue
        if row["impairment"] not in (impairment, "none"):
            continue
        probability = float(row["probability"])
        points.append((probability, float(row[metric]), row["run_id"], row[metric]))
    points.sort(key=lambda point: point[0])
    return points


def render_figure(
    rows: list[dict[str, str]], metric: str, impairment: str
) -> str:
    """Builds the complete SVG document for one metric and impairment path."""
    axis_label, scale = PLOTTED_METRICS[metric]
    series = {
        protocol: series_for(rows, protocol, impairment, metric)
        for protocol in PROTOCOLS
    }
    values = [point[1] for points in series.values() for point in points]
    if not values:
        raise PlotError(f"no data for {metric} under {impairment}")
    axis = Axis(values, scale)

    title = f"{axis_label.split(' (')[0]} versus {IMPAIRMENT_TITLES[impairment]}"
    description = (
        f"Three ARQ protocols compared as the {IMPAIRMENT_TITLES[impairment].lower()} "
        f"probability rises from 0.0 to 0.5, all other impairment paths held at "
        f"zero. Source: results/experiments.csv."
    )
    if metric == "efficiency":
        description = f"{description} {EFFICIENCY_DEFINITION}"

    parts: list[str] = [
        f'<svg xmlns="{SVG_NAMESPACE}" width="{WIDTH}" height="{HEIGHT}" '
        f'viewBox="0 0 {WIDTH} {HEIGHT}" role="img">',
        f"<title>{escape(title)}</title>",
        f"<desc>{escape(description)}</desc>",
        f'<rect x="0" y="0" width="{WIDTH}" height="{HEIGHT}" fill="#ffffff"/>',
        '<g font-family="Helvetica, Arial, sans-serif" fill="#1f2933">',
        f'<text x="{WIDTH / 2:g}" y="30" text-anchor="middle" font-size="17" '
        f'font-weight="bold">{escape(title)}</text>',
        f'<text x="{WIDTH / 2:g}" y="50" text-anchor="middle" font-size="12" '
        f'fill="#5b6670">{escape("CRC-16, 46-byte payload, 4096-byte input, window 8, seed 20260831")}</text>',
    ]

    # Horizontal gridlines and y-axis tick labels.
    for tick in axis.ticks:
        y = axis.position(tick)
        parts.append(
            f'<line x1="{PLOT_LEFT}" y1="{coordinate(y)}" x2="{PLOT_RIGHT}" '
            f'y2="{coordinate(y)}" stroke="#dde3e8" stroke-width="1"/>'
        )
        parts.append(
            f'<text x="{PLOT_LEFT - 10}" y="{coordinate(y + 4)}" text-anchor="end" '
            f'font-size="11" fill="#5b6670">{escape(format_tick(tick))}</text>'
        )

    # Axis lines.
    parts.append(
        f'<line x1="{PLOT_LEFT}" y1="{PLOT_TOP}" x2="{PLOT_LEFT}" '
        f'y2="{PLOT_BOTTOM}" stroke="#1f2933" stroke-width="1.5"/>'
    )
    parts.append(
        f'<line x1="{PLOT_LEFT}" y1="{PLOT_BOTTOM}" x2="{PLOT_RIGHT}" '
        f'y2="{PLOT_BOTTOM}" stroke="#1f2933" stroke-width="1.5"/>'
    )

    # X-axis tick labels.
    for probability in X_VALUES:
        x = x_position(probability)
        label = "0.0\n(none)" if probability == 0.0 else f"{probability:.1f}"
        parts.append(
            f'<line x1="{coordinate(x)}" y1="{PLOT_BOTTOM}" x2="{coordinate(x)}" '
            f'y2="{PLOT_BOTTOM + 5}" stroke="#1f2933" stroke-width="1"/>'
        )
        parts.append(
            f'<text x="{coordinate(x)}" y="{PLOT_BOTTOM + 20}" text-anchor="middle" '
            f'font-size="11">{escape(label.split(chr(10))[0])}</text>'
        )
        if "\n" in label:
            parts.append(
                f'<text x="{coordinate(x)}" y="{PLOT_BOTTOM + 33}" '
                f'text-anchor="middle" font-size="10" fill="#5b6670">'
                f'{escape(label.split(chr(10))[1])}</text>'
            )

    # Axis titles.
    parts.append(
        f'<text x="{(PLOT_LEFT + PLOT_RIGHT) / 2:g}" y="{PLOT_BOTTOM + 56}" '
        f'text-anchor="middle" font-size="12">'
        f'{escape(IMPAIRMENT_TITLES[impairment])} probability</text>'
    )
    parts.append(
        f'<text transform="translate(22 {(PLOT_TOP + PLOT_BOTTOM) / 2:g}) rotate(-90)" '
        f'text-anchor="middle" font-size="12">{escape(axis_label)}</text>'
    )

    # One polyline plus labelled markers per protocol.
    for protocol in PROTOCOLS:
        colour, label, dashes, radius, filled = SERIES_STYLE[protocol]
        marker_paint = (
            f'fill="{colour}"'
            if filled
            else f'fill="none" stroke="{colour}" stroke-width="2.2"'
        )
        points = series[protocol]
        polyline = " ".join(
            f"{coordinate(x_position(probability))},{coordinate(axis.position(value))}"
            for probability, value, _, _ in points
        )
        parts.append(
            f'<polyline points="{polyline}" fill="none" stroke="{colour}" '
            f'stroke-width="2" stroke-linejoin="round" stroke-dasharray="{dashes}"/>'
        )
        for probability, value, run_id, raw in points:
            x = x_position(probability)
            y = axis.position(value)
            parts.append(
                f'<circle cx="{coordinate(x)}" cy="{coordinate(y)}" r="{radius:g}" '
                f'{marker_paint} data-run={quoteattr(run_id)} '
                f'data-protocol={quoteattr(protocol)} '
                f'data-probability={quoteattr(f"{probability:.1f}")} '
                f'data-metric={quoteattr(metric)} data-value={quoteattr(raw)}>'
                f"<title>{escape(f'{label} at {probability:.1f}: {raw}')}</title>"
                f"</circle>"
            )

    # Legend along the bottom.
    legend_y = HEIGHT - 22
    legend_x = PLOT_LEFT
    for protocol in PROTOCOLS:
        colour, label, dashes, radius, filled = SERIES_STYLE[protocol]
        marker_paint = (
            f'fill="{colour}"'
            if filled
            else f'fill="none" stroke="{colour}" stroke-width="2.2"'
        )
        parts.append(
            f'<line x1="{legend_x}" y1="{legend_y - 4}" x2="{legend_x + 22}" '
            f'y2="{legend_y - 4}" stroke="{colour}" stroke-width="2" '
            f'stroke-dasharray="{dashes}"/>'
        )
        parts.append(
            f'<circle cx="{legend_x + 11}" cy="{legend_y - 4}" r="{radius:g}" '
            f"{marker_paint}/>"
        )
        parts.append(
            f'<text x="{legend_x + 29}" y="{legend_y}" font-size="12">'
            f"{escape(label)}</text>"
        )
        legend_x += 190

    parts.append("</g>")
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="plot_results.py",
        description="Render validated experiment results as SVG figures.",
        epilog=EFFICIENCY_DEFINITION,
    )
    parser.add_argument("--input", default=str(DEFAULT_INPUT))
    parser.add_argument("--output-directory", default=str(DEFAULT_OUTPUT_DIRECTORY))
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)

    try:
        rows = load_rows(Path(arguments.input))
    except ResultsError as error:
        print(f"plot_results: {error}", file=sys.stderr)
        return 1

    violations, _ = validate(rows)
    if violations:
        for violation in violations:
            print(f"VIOLATION: {violation}", file=sys.stderr)
        print(
            "plot_results: refusing to plot results that fail validation",
            file=sys.stderr,
        )
        return 1

    output_directory = Path(arguments.output_directory)
    output_directory.mkdir(parents=True, exist_ok=True)

    written = 0
    for metric in PLOTTED_METRICS:
        for impairment in IMPAIRMENTS:
            try:
                document = render_figure(rows, metric, impairment)
            except PlotError as error:
                print(f"plot_results: {error}", file=sys.stderr)
                return 1
            path = output_directory / f"{metric}_vs_probability_{impairment}.svg"
            path.write_text(document, encoding="utf-8")
            written += 1

    print(f"{output_directory}: {written} SVG figures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
