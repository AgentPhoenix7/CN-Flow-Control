#!/usr/bin/env python3
"""Runs C++-basic's sender/receiver across multiple input files, all three ARQ
protocols, and a matrix of channel-impairment cases, recording one row of
metrics per run to results/experiments.csv.

Matrix design (one-factor-at-a-time, the same idea the full C++/ project's own
experiment matrix uses): for each input file and each protocol, run one clean
baseline (0% error/loss on both paths), then sweep each of the four impairment
paths (data-error, data-loss, ack-error, ack-loss) through probabilities
0.1-0.5 while holding the other three paths at 0. The same (input file, path,
probability) combination uses the same channel RNG seeds across all three
protocols, so any difference between protocols in the results is caused by
protocol logic, not by different protocols getting different random luck.

Every run's reconstructed output is required to be byte-identical to the
input; the whole matrix aborts (with the CSV rows written so far left intact)
on the first run that fails or produces a mismatched file.

Usage:
    python3 tools/run_experiments.py
    python3 tools/run_experiments.py --window 16 --timeout 60
    python3 tools/run_experiments.py --inputs-dir test_data --output results/experiments.csv
"""
from __future__ import annotations

import argparse
import csv
import pathlib
import re
import subprocess
import sys
import time
import zlib

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
BUILD_DIR = PROJECT_DIR / "build"
SENDER = BUILD_DIR / "sender"
RECEIVER = BUILD_DIR / "receiver"

# header(15) + payload(46) + CRC-16(2), from include/frame.hpp's FRAME_BODY_LEN.
FRAME_WIRE_BYTES = 63

PROTOCOLS = ["stop-and-wait", "go-back-n", "selective-repeat"]
IMPAIRMENT_PATHS = ["data-error", "data-loss", "ack-error", "ack-loss"]
PROBABILITIES = [0.1, 0.2, 0.3, 0.4, 0.5]

METRICS_RE = re.compile(
    r"protocol=(?P<protocol>\S+) elapsed_ms=(?P<elapsed_ms>[0-9.]+) "
    r"frames_sent=(?P<frames_sent>\d+) retransmissions=(?P<retransmissions>\d+) "
    r"acks_received=(?P<acks_received>\d+) timeouts=(?P<timeouts>\d+)"
)


def ensure_built() -> None:
    if SENDER.exists() and RECEIVER.exists():
        return
    print("==> sender/receiver not built yet, running 'make all'...", file=sys.stderr)
    result = subprocess.run(["make", "-C", str(PROJECT_DIR), "all"])
    if result.returncode != 0 or not SENDER.exists() or not RECEIVER.exists():
        raise SystemExit("build failed; run 'make -C C++-basic all' and check the errors")


def deterministic_seed(*parts: str) -> int:
    """A stable seed derived from 'parts', independent of Python's randomized
    string hashing, so the same (input, path, probability) key always
    produces the same seed on every run and for every protocol -- which is
    what makes comparing protocols at a given point in the matrix fair.

    Masked to 31 bits (not the full 32-bit CRC range): the sender/receiver
    CLIs parse --seed with C++'s std::stoi() into a signed 32-bit int, which
    throws (and crashes the process) for any value above INT32_MAX."""
    key = "|".join(parts).encode("utf-8")
    return zlib.crc32(key) & 0x7FFFFFFF


def run_once(
    input_path: pathlib.Path,
    protocol: str,
    window: int,
    cfg: dict[str, float],
    port: int,
    seed_key: str,
    timeout_s: float,
) -> dict[str, float | int]:
    data_seed = deterministic_seed(seed_key, "data")
    ack_seed = deterministic_seed(seed_key, "ack")
    output_path = PROJECT_DIR / "results" / "raw" / "last_received.bin"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    window_args = [] if protocol == "stop-and-wait" else ["--window", str(window)]

    receiver_cmd = [
        str(RECEIVER), "--protocol", protocol, "--port", str(port),
        "--output", str(output_path), *window_args,
        "--ack-error", str(cfg["ack-error"]), "--ack-loss", str(cfg["ack-loss"]),
        "--seed", str(ack_seed),
    ]
    receiver = subprocess.Popen(receiver_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    time.sleep(0.2)  # give the receiver time to reach listen(), same as run_demo.sh

    sender_cmd = [
        str(SENDER), "--protocol", protocol, "--port", str(port),
        "--input", str(input_path), *window_args,
        "--data-error", str(cfg["data-error"]), "--data-loss", str(cfg["data-loss"]),
        "--seed", str(data_seed),
    ]
    try:
        sender = subprocess.run(sender_cmd, capture_output=True, text=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        receiver.kill()
        receiver.communicate()
        raise RuntimeError(f"sender timed out after {timeout_s}s: {' '.join(sender_cmd)}")

    try:
        _, receiver_stderr = receiver.communicate(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        receiver.kill()
        receiver.communicate()
        raise RuntimeError(f"receiver did not exit: {' '.join(receiver_cmd)}")

    # Check the receiver first: if it crashed or exited before accepting a
    # connection, the sender's own error ("connect() failed: Connection
    # refused") is just a symptom, not the actual cause.
    if receiver.returncode != 0:
        raise RuntimeError(f"receiver failed (exit {receiver.returncode}): {receiver_stderr.strip()}")
    if sender.returncode != 0:
        raise RuntimeError(f"sender failed (exit {sender.returncode}): {sender.stderr.strip()}")

    match = METRICS_RE.search(sender.stdout)
    if not match:
        raise RuntimeError(f"could not parse sender output: {sender.stdout!r}")

    if output_path.read_bytes() != input_path.read_bytes():
        raise RuntimeError(f"received file is not byte-identical to {input_path}")

    groups = match.groupdict()
    return {
        "elapsed_ms": float(groups["elapsed_ms"]),
        "frames_sent": int(groups["frames_sent"]),
        "retransmissions": int(groups["retransmissions"]),
        "acks_received": int(groups["acks_received"]),
        "timeouts": int(groups["timeouts"]),
    }


def build_matrix(inputs: list[pathlib.Path], protocols: list[str]):
    """Yields (input_path, protocol, impairment_path, probability, cfg) for the
    full one-factor-at-a-time matrix: one clean baseline plus, for each of the
    four impairment paths, probabilities 0.1-0.5 with the other three paths at 0."""
    zero_cfg = {"data-error": 0.0, "data-loss": 0.0, "ack-error": 0.0, "ack-loss": 0.0}
    for input_path in inputs:
        for protocol in protocols:
            yield input_path, protocol, "baseline", 0.0, dict(zero_cfg)
            for path in IMPAIRMENT_PATHS:
                for p in PROBABILITIES:
                    cfg = dict(zero_cfg)
                    cfg[path] = p
                    yield input_path, protocol, path, p, cfg


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--inputs-dir", type=pathlib.Path, default=PROJECT_DIR / "test_data")
    parser.add_argument("--window", type=int, default=8, help="Go-Back-N/Selective Repeat window size")
    parser.add_argument("--output", type=pathlib.Path, default=PROJECT_DIR / "results" / "experiments.csv")
    parser.add_argument("--base-port", type=int, default=6000)
    parser.add_argument("--timeout", type=float, default=30.0, help="per-run subprocess timeout, seconds")
    args = parser.parse_args()

    ensure_built()

    inputs = sorted(p for p in args.inputs_dir.iterdir() if p.is_file())
    if not inputs:
        raise SystemExit(f"no input files found in {args.inputs_dir} (run tools/generate_inputs.py first)")

    matrix = list(build_matrix(inputs, PROTOCOLS))
    total = len(matrix)
    print(f"==> {total} runs across {len(inputs)} input file(s) and {len(PROTOCOLS)} protocol(s): "
          f"{', '.join(p.name for p in inputs)}", file=sys.stderr)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "input_file", "input_size_bytes", "protocol", "window",
        "impairment_path", "probability", "elapsed_ms", "frames_sent",
        "retransmissions", "acks_received", "timeouts",
        "unique_payload_bytes", "transmitted_frame_bytes",
        "efficiency", "goodput_bytes_per_second",
    ]

    start = time.time()
    with args.output.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        port = args.base_port
        for i, (input_path, protocol, path, p, cfg) in enumerate(matrix, start=1):
            seed_key = f"{input_path.name}|{path}|{p}"
            print(f"[{i}/{total}] {input_path.name:12s} {protocol:16s} {path:10s} p={p:.1f} port={port} ... ",
                  file=sys.stderr, end="", flush=True)

            try:
                result = run_once(input_path, protocol, args.window, cfg, port, seed_key, args.timeout)
            except Exception as exc:  # noqa: BLE001 -- deliberately broad: any failure aborts the matrix
                print(f"FAILED: {exc}", file=sys.stderr)
                print(f"==> wrote {i - 1}/{total} rows to {args.output} before failing", file=sys.stderr)
                raise SystemExit(1) from exc
            port += 1

            input_size = input_path.stat().st_size
            transmitted_frame_bytes = result["frames_sent"] * FRAME_WIRE_BYTES
            efficiency = (input_size / transmitted_frame_bytes) if transmitted_frame_bytes else 0.0
            goodput = (input_size / (result["elapsed_ms"] / 1000.0)) if result["elapsed_ms"] > 0 else 0.0

            row = {
                "input_file": input_path.name,
                "input_size_bytes": input_size,
                "protocol": protocol,
                "window": 1 if protocol == "stop-and-wait" else args.window,
                "impairment_path": path,
                "probability": p,
                "unique_payload_bytes": input_size,
                "transmitted_frame_bytes": transmitted_frame_bytes,
                "efficiency": round(efficiency, 6),
                "goodput_bytes_per_second": round(goodput, 3),
                **result,
            }
            writer.writerow(row)
            f.flush()
            print(f"elapsed_ms={result['elapsed_ms']:.1f} frames_sent={result['frames_sent']} "
                  f"retransmissions={result['retransmissions']} timeouts={result['timeouts']}", file=sys.stderr)

    elapsed = time.time() - start
    print(f"==> wrote {total} rows to {args.output} in {elapsed:.1f}s", file=sys.stderr)


if __name__ == "__main__":
    main()
