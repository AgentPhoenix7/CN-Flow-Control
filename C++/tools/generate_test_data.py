#!/usr/bin/env python3
"""Generates the deterministic binary fixtures used by transfers and experiments.

The fixture must be a pure function of its seed and size so that an experiment
run recorded in ``results/experiments.csv`` can be replayed byte for byte later.
``random.Random(seed).randbytes(size)`` satisfies that: the Mersenne Twister is
specified by CPython, is stable across platforms and versions, and fills bytes
low word first, so a longer fixture extends a shorter one with the same seed.

The committed ``test_data/input.bin`` is exactly ``--seed 20260831 --size 4096``.
That size is deliberate: 4096 is not a multiple of 46 or 100, so both leave a
short final frame, while 64 divides it exactly.

Usage:
    uv run python C++/tools/generate_test_data.py --output <path>
                  [--size <bytes>] [--seed <unsigned>]
"""

from __future__ import annotations

import argparse
import random
import sys
from pathlib import Path

#: Seed and size that reproduce the committed ``test_data/input.bin``.
DEFAULT_SEED = 20260831
DEFAULT_SIZE = 4096

#: Largest fixture the tool will produce, a guard against a mistyped size.
MAXIMUM_SIZE = 64 * 1024 * 1024


def generate_bytes(seed: int, size: int) -> bytes:
    """Returns the deterministic fixture body for ``seed`` and ``size``."""
    if seed < 0:
        raise ValueError(f"--seed must not be negative: {seed}")
    if size < 1:
        raise ValueError(f"--size must be at least 1 byte: {size}")
    if size > MAXIMUM_SIZE:
        raise ValueError(f"--size must be at most {MAXIMUM_SIZE} bytes: {size}")

    return random.Random(seed).randbytes(size)


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    """Parses and range-checks the command line, exiting nonzero on any error."""
    parser = argparse.ArgumentParser(
        prog="generate_test_data.py",
        description="Generate a deterministic binary transfer fixture.",
    )
    parser.add_argument("--output", required=True, help="path of the fixture to write")
    parser.add_argument(
        "--size",
        type=int,
        default=DEFAULT_SIZE,
        help=f"fixture size in bytes (default {DEFAULT_SIZE})",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=DEFAULT_SEED,
        help=f"deterministic generator seed (default {DEFAULT_SEED})",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)

    try:
        # Generated before the file is touched, so a rejected command line
        # never leaves a partially written fixture behind.
        body = generate_bytes(arguments.seed, arguments.size)
    except ValueError as error:
        print(f"generate_test_data: {error}", file=sys.stderr)
        return 2

    output = Path(arguments.output)
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(body)
    except OSError as error:
        print(f"generate_test_data: cannot write {output}: {error}", file=sys.stderr)
        return 1

    print(f"{output}: {len(body)} bytes from seed {arguments.seed}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
