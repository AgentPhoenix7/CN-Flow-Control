#!/usr/bin/env python3
"""Renders SVG line charts from results/experiments.csv (written by
tools/run_experiments.py). No external dependencies -- every chart is drawn
by hand as plain SVG using only the Python standard library, so no plotting
library needs to be installed to view them (any browser renders SVG).

Usage:
    python3 tools/plot_results.py
    python3 tools/plot_results.py --csv results/experiments.csv --out-dir results/plots
"""
from __future__ import annotations

import argparse
import csv
import pathlib
import statistics

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent

PROTOCOLS = ["stop-and-wait", "go-back-n", "selective-repeat"]
IMPAIRMENT_PATHS = ["data-error", "data-loss", "ack-error", "ack-loss"]
PROBABILITIES = [0.0, 0.1, 0.2, 0.3, 0.4, 0.5]  # 0.0 is served by each protocol's baseline row

# Colour + marker shape together (not colour alone) distinguish the three
# protocols, so the charts stay legible for colourblind readers and in print.
STYLE_COLOR = {
    "stop-and-wait": {"color": "#2563eb", "marker": "circle", "dash": None},
    "go-back-n": {"color": "#f97316", "marker": "square", "dash": None},
    "selective-repeat": {"color": "#16a34a", "marker": "triangle", "dash": None},
}

# Pure black, distinguished only by marker shape and line dash pattern --
# for print/report use where colour cannot be relied on at all.
STYLE_MONO = {
    "stop-and-wait": {"color": "#000000", "marker": "circle", "dash": None},
    "go-back-n": {"color": "#000000", "marker": "square", "dash": "7,4"},
    "selective-repeat": {"color": "#000000", "marker": "triangle", "dash": "2,3"},
}

STYLE = STYLE_COLOR  # overridden by main() when --mono is passed


# ------------------------------------------------------------------ data --

def load_rows(csv_path: pathlib.Path) -> list[dict]:
    with csv_path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    for row in rows:
        row["probability"] = float(row["probability"])
        row["efficiency"] = float(row["efficiency"])
        row["elapsed_ms"] = float(row["elapsed_ms"])
        row["retransmissions"] = int(row["retransmissions"])
        row["input_size_bytes"] = int(row["input_size_bytes"])
    return rows


def mean_by(rows: list[dict], protocol: str, impairment_path: str, probability: float, field: str) -> float | None:
    """Averages 'field' across every input file for one (protocol, path,
    probability) combination. probability == 0.0 always reads the shared
    'baseline' rows, regardless of which path's chart is asking."""
    key_path = "baseline" if probability == 0.0 else impairment_path
    values = [
        r[field] for r in rows
        if r["protocol"] == protocol and r["impairment_path"] == key_path and r["probability"] == probability
    ]
    return statistics.mean(values) if values else None


# ------------------------------------------------------------------- SVG --

def _marker(x: float, y: float, shape: str, color: str) -> str:
    if shape == "circle":
        return f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}" />'
    if shape == "square":
        return f'<rect x="{x - 3.5:.1f}" y="{y - 3.5:.1f}" width="7" height="7" fill="{color}" />'
    return (  # triangle
        f'<polygon points="{x:.1f},{y - 4.5:.1f} {x - 4:.1f},{y + 3.5:.1f} {x + 4:.1f},{y + 3.5:.1f}" '
        f'fill="{color}" />'
    )


def render_line_chart(
    out_path: pathlib.Path,
    title: str,
    subtitle: str,
    x_label: str,
    y_label: str,
    x_values: list[float],
    series: dict[str, dict[float, float | None]],
) -> None:
    """series: {series_label: {x_value: y_value_or_None}}. A None/missing
    y-value simply breaks that point out of its line instead of crashing, so a
    chart can still be rendered from a partial (e.g. aborted-mid-run) CSV."""
    width, height = 760, 440
    margin_left, margin_right, margin_top, margin_bottom = 70, 30, 50, 60
    plot_w = width - margin_left - margin_right
    plot_h = height - margin_top - margin_bottom

    all_y = [y for points in series.values() for y in points.values() if y is not None]
    y_min = 0.0
    y_max = (max(all_y) * 1.15) if all_y else 1.0
    if y_max <= y_min:
        y_max = y_min + 1.0

    x_min, x_max = min(x_values), max(x_values)
    x_span = (x_max - x_min) or 1.0

    def sx(x: float) -> float:
        return margin_left + (x - x_min) / x_span * plot_w

    def sy(y: float) -> float:
        return margin_top + plot_h - (y - y_min) / (y_max - y_min) * plot_h

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}">',
        f"<title>{title}</title><desc>{subtitle}</desc>",
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="white" />',
        f'<text x="{width / 2:.0f}" y="24" text-anchor="middle" font-size="16" '
        f'font-family="sans-serif" font-weight="bold">{title}</text>',
        f'<text x="{width / 2:.0f}" y="42" text-anchor="middle" font-size="11" '
        f'font-family="sans-serif" fill="#555">{subtitle}</text>',
        f'<line x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" '
        f'y2="{margin_top + plot_h}" stroke="#333" />',
        f'<line x1="{margin_left}" y1="{margin_top + plot_h}" '
        f'x2="{margin_left + plot_w}" y2="{margin_top + plot_h}" stroke="#333" />',
    ]

    for i in range(6):
        y_val = y_min + (y_max - y_min) * i / 5
        y = sy(y_val)
        parts.append(
            f'<line x1="{margin_left}" y1="{y:.1f}" x2="{margin_left + plot_w}" y2="{y:.1f}" stroke="#eee" />'
        )
        parts.append(
            f'<text x="{margin_left - 8}" y="{y + 4:.1f}" text-anchor="end" '
            f'font-size="10" font-family="sans-serif">{y_val:.3g}</text>'
        )

    for x_val in x_values:
        x = sx(x_val)
        parts.append(
            f'<line x1="{x:.1f}" y1="{margin_top + plot_h}" x2="{x:.1f}" '
            f'y2="{margin_top + plot_h + 5}" stroke="#333" />'
        )
        parts.append(
            f'<text x="{x:.1f}" y="{margin_top + plot_h + 18}" text-anchor="middle" '
            f'font-size="10" font-family="sans-serif">{x_val:g}</text>'
        )

    parts.append(
        f'<text x="{margin_left + plot_w / 2:.0f}" y="{height - 8}" text-anchor="middle" '
        f'font-size="12" font-family="sans-serif">{x_label}</text>'
    )
    parts.append(
        f'<text x="14" y="{margin_top + plot_h / 2:.0f}" text-anchor="middle" font-size="12" '
        f'font-family="sans-serif" transform="rotate(-90 14 {margin_top + plot_h / 2:.0f})">{y_label}</text>'
    )

    legend_y = margin_top
    for label, points in series.items():
        style = STYLE.get(label, {"color": "#666", "marker": "circle", "dash": None})
        dash_attr = f' stroke-dasharray="{style["dash"]}"' if style.get("dash") else ""
        coords = [(x, points.get(x)) for x in x_values if points.get(x) is not None]
        if coords:
            path_d = " ".join(f'{"M" if i == 0 else "L"}{sx(x):.1f},{sy(y):.1f}' for i, (x, y) in enumerate(coords))
            parts.append(f'<path d="{path_d}" fill="none" stroke="{style["color"]}" stroke-width="2"{dash_attr} />')
            for x, y in coords:
                parts.append(_marker(sx(x), sy(y), style["marker"], style["color"]))
        # Legend entry: a short sample line (with the series' own dash pattern)
        # plus its marker, not a solid colour swatch -- stays legible even when
        # every series uses the same colour (the monochrome style).
        legend_y += 18
        ly = legend_y - 4
        parts.append(
            f'<line x1="{width - 172}" y1="{ly}" x2="{width - 150}" y2="{ly}" '
            f'stroke="{style["color"]}" stroke-width="2"{dash_attr} />'
        )
        parts.append(_marker(width - 161, ly, style["marker"], style["color"]))
        parts.append(f'<text x="{width - 145}" y="{legend_y}" font-size="11" font-family="sans-serif">{label}</text>')

    parts.append("</svg>")
    out_path.write_text("\n".join(parts), encoding="utf-8")


# ------------------------------------------------------------------ main --

def main() -> None:
    global STYLE

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv", type=pathlib.Path, default=PROJECT_DIR / "results" / "experiments.csv")
    parser.add_argument("--out-dir", type=pathlib.Path, default=None,
                         help="default: results/plots, or results/plots_mono with --mono")
    parser.add_argument("--mono", action="store_true",
                         help="pure black lines, distinguished only by marker shape and dash pattern "
                              "(for print/report use) instead of the default colour + shape style")
    args = parser.parse_args()

    if args.mono:
        STYLE = STYLE_MONO
    if args.out_dir is None:
        args.out_dir = PROJECT_DIR / "results" / ("plots_mono" if args.mono else "plots")

    if not args.csv.exists():
        raise SystemExit(f"{args.csv} not found -- run tools/run_experiments.py first")

    rows = load_rows(args.csv)
    if not rows:
        raise SystemExit(f"{args.csv} has no data rows")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    n_inputs = len({r["input_file"] for r in rows})
    subtitle_avg = f"mean over {n_inputs} input file(s); same channel seed per point across all 3 protocols"

    written: list[pathlib.Path] = []

    metric_specs = [
        ("efficiency", "efficiency_vs_probability_{}.svg", "Efficiency vs {} probability",
         "probability", "efficiency (payload bytes / wire bytes)"),
        ("elapsed_ms", "completion_ms_vs_probability_{}.svg", "Completion time vs {} probability",
         "probability", "elapsed time (ms)"),
        ("retransmissions", "retransmissions_vs_probability_{}.svg", "Retransmissions vs {} probability",
         "probability", "retransmissions"),
    ]
    for field, filename_tpl, title_tpl, x_label, y_label in metric_specs:
        for path in IMPAIRMENT_PATHS:
            series = {protocol: {p: mean_by(rows, protocol, path, p, field) for p in PROBABILITIES}
                      for protocol in PROTOCOLS}
            out_path = args.out_dir / filename_tpl.format(path)
            render_line_chart(out_path, title_tpl.format(path), subtitle_avg, x_label, y_label, PROBABILITIES, series)
            written.append(out_path)

    # Efficiency vs input file size, clean baseline only -- the one chart that
    # foregrounds the "multiple input files" dimension directly.
    sizes = sorted({r["input_size_bytes"] for r in rows})
    series = {}
    for protocol in PROTOCOLS:
        series[protocol] = {}
        for size in sizes:
            values = [
                r["efficiency"] for r in rows
                if r["protocol"] == protocol and r["impairment_path"] == "baseline" and r["input_size_bytes"] == size
            ]
            series[protocol][size] = statistics.mean(values) if values else None
    out_path = args.out_dir / "efficiency_vs_input_size_baseline.svg"
    render_line_chart(out_path, "Efficiency vs input file size (clean baseline)", "no impairment on either path",
                       "input size (bytes)", "efficiency (payload bytes / wire bytes)", sizes, series)
    written.append(out_path)

    for path in written:
        print(f"wrote {path}")
    print(f"==> {len(written)} SVG figures in {args.out_dir}")


if __name__ == "__main__":
    main()
