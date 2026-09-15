#!/usr/bin/env python3
"""Generates a small, deterministic set of extra input files spanning a range
of sizes and content types, for tools/run_experiments.py's "multiple input
files" matrix. Existing test_data/input.txt is left untouched.

Usage:
    python3 tools/generate_inputs.py [--out-dir test_data] [--seed 20260913]
"""
from __future__ import annotations

import argparse
import pathlib
import random

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
DEFAULT_OUT_DIR = SCRIPT_DIR.parent / "test_data"

# Sizes chosen so the resulting frame counts (payload is a fixed 46 bytes,
# see include/frame.hpp's PAYLOAD_LEN) stay small enough that the full
# impairment matrix in run_experiments.py finishes in a reasonable time even
# under heavy simulated loss.
TINY_TEXT = (
    b"Tiny two-frame file for the experiment matrix.\n"
)  # 47 bytes -> 2 frames (46-byte payload each)
MEDIUM_SIZE = 1200  # -> 27 frames (1199/46 = 26.07 -> 27)


def make_medium_binary(seed: int, size: int) -> bytes:
    return random.Random(seed).randbytes(size)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", type=pathlib.Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--seed", type=int, default=20260913)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    tiny_path = args.out_dir / "tiny.txt"
    tiny_path.write_bytes(TINY_TEXT)

    medium_path = args.out_dir / "medium.bin"
    medium_path.write_bytes(make_medium_binary(args.seed, MEDIUM_SIZE))

    for path in (tiny_path, medium_path):
        print(f"wrote {path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
