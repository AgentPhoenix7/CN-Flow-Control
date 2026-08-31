"""Determinism and CLI-validation checks for ``tools/generate_test_data.py``.

The experiment matrix is only reproducible if its input fixture is. These
cases require the generator to be a pure function of ``--seed`` and
``--size``, to reproduce the committed ``test_data/input.bin`` byte for byte,
and to reject invalid command lines without writing a partial file.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

CPP_DIR = Path(__file__).resolve().parent.parent
GENERATOR = CPP_DIR / "tools" / "generate_test_data.py"
COMMITTED_FIXTURE = CPP_DIR / "test_data" / "input.bin"

# The seed and size that produced the committed fixture in Task 6.
FIXTURE_SEED = 20260831
FIXTURE_SIZE = 4096

TOOL_TIMEOUT_SECONDS = 60


class GeneratorError(AssertionError):
    """Raised when the fixture generator misbehaves."""


def run_generator(arguments: list[str]) -> subprocess.CompletedProcess:
    """Runs the generator with the project's ``uv run python`` convention."""
    return subprocess.run(
        [sys.executable, str(GENERATOR), *arguments],
        capture_output=True,
        text=True,
        timeout=TOOL_TIMEOUT_SECONDS,
        cwd=str(CPP_DIR),
    )


def generate(directory: Path, name: str, *, seed: int, size: int) -> bytes:
    """Generates one fixture and returns its bytes, failing on a bad exit."""
    output = directory / name
    result = run_generator(
        ["--output", str(output), "--seed", str(seed), "--size", str(size)]
    )
    if result.returncode != 0:
        raise GeneratorError(f"generator exited {result.returncode}: {result.stderr}")
    if not output.exists():
        raise GeneratorError(f"generator did not write {output}")
    return output.read_bytes()


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
    if not GENERATOR.exists():
        print(f"FAIL: missing tool {GENERATOR}", file=sys.stderr)
        return 1

    suite = Suite()

    with tempfile.TemporaryDirectory() as raw_directory:
        directory = Path(raw_directory)

        def same_seed_is_byte_identical() -> None:
            first = generate(directory, "first.bin", seed=7, size=1024)
            second = generate(directory, "second.bin", seed=7, size=1024)
            if first != second:
                raise GeneratorError("two runs with the same seed differed")
            if len(first) != 1024:
                raise GeneratorError(f"expected 1024 bytes, got {len(first)}")

        suite.case("same seed produces byte-identical output", same_seed_is_byte_identical)

        def different_seed_differs() -> None:
            first = generate(directory, "seed_a.bin", seed=11, size=1024)
            second = generate(directory, "seed_b.bin", seed=12, size=1024)
            if first == second:
                raise GeneratorError("two different seeds produced identical output")

        suite.case("different seeds produce different output", different_seed_differs)

        def sizes_are_exact() -> None:
            for size in (1, 46, 64, 4096):
                data = generate(directory, f"size_{size}.bin", seed=3, size=size)
                if len(data) != size:
                    raise GeneratorError(f"asked for {size} bytes, got {len(data)}")

        suite.case("requested sizes are exact", sizes_are_exact)

        def prefix_is_stable() -> None:
            # A longer fixture must extend the shorter one, so a size change
            # does not silently reshuffle every byte of the schedule.
            short = generate(directory, "short.bin", seed=5, size=256)
            long = generate(directory, "long.bin", seed=5, size=512)
            if long[:256] != short:
                raise GeneratorError("a longer fixture did not extend the shorter one")

        suite.case("a longer fixture extends the shorter one", prefix_is_stable)

        def reproduces_committed_fixture() -> None:
            if not COMMITTED_FIXTURE.exists():
                raise GeneratorError(f"missing committed fixture {COMMITTED_FIXTURE}")
            regenerated = generate(
                directory, "committed.bin", seed=FIXTURE_SEED, size=FIXTURE_SIZE
            )
            if regenerated != COMMITTED_FIXTURE.read_bytes():
                raise GeneratorError(
                    "regenerating with the documented seed did not reproduce "
                    "test_data/input.bin"
                )

        suite.case(
            "documented seed reproduces the committed fixture",
            reproduces_committed_fixture,
        )

        rejections = [
            ("zero size", ["--output", str(directory / "bad.bin"), "--size", "0"]),
            ("negative size", ["--output", str(directory / "bad.bin"), "--size", "-1"]),
            ("non-integer size", ["--output", str(directory / "bad.bin"), "--size", "big"]),
            ("negative seed", ["--output", str(directory / "bad.bin"), "--seed", "-1"]),
            ("missing output path", ["--size", "16"]),
            ("unknown option", ["--output", str(directory / "bad.bin"), "--turbo", "1"]),
        ]
        for name, arguments in rejections:
            result = run_generator(arguments)
            suite.check(
                f"generator rejects {name}",
                result.returncode != 0 and bool(result.stderr.strip()),
                f"exit {result.returncode}, stderr {result.stderr!r}",
            )

        def rejection_writes_nothing() -> None:
            target = directory / "never_written.bin"
            result = run_generator(["--output", str(target), "--size", "0"])
            if result.returncode == 0:
                raise GeneratorError("a zero size was accepted")
            if target.exists():
                raise GeneratorError("a rejected command line still wrote a file")

        suite.case("a rejected command line writes no file", rejection_writes_nothing)

    if suite.failures != 0:
        print(f"FAIL: {suite.failures} fixture case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
