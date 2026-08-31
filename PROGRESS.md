# Project Progress and Conversation Handoff

Last updated: **31 August 2026**

## How to Resume in a New Conversation

Read `AGENTS.md` and this file, inspect the current working tree, and continue from the exact current step below. Treat the assignment PDF and current source as authoritative evidence. This is a guided C++17 project: do not implement C++ code unless the user explicitly requests it, and normally proceed in small testable steps.

Suggested resume request:

> Read `AGENTS.md`, `PROGRESS.md`, and inspect the repository. Continue the guided C++17 implementation from the exact current step. Do not implement code unless I explicitly request it.

## Assignment Objective

Build sender and receiver programs that transfer framed file data over a bidirectional socket while simulating Data Link Layer flow control. Implement and compare:

- Stop-and-Wait;
- Go-Back-N ARQ; and
- Selective Repeat ARQ.

The simulated channel must support reproducible bit corruption and excessive delay/loss on both DATA and ACK paths. The receiver verifies Assignment 1 checksum/CRC protection before accepting data. Timers, acknowledgments, duplicate handling, window movement, buffering, and retransmission operate at the application layer rather than relying on TCP reliability.

Demonstration: **24–28 August 2026**. Report submission: **31 August–4 September 2026**.

## Confirmed Design Decisions

- Implementation language: C++17 with `g++` and strict warnings.
- Active implementation directory: `C++/`; headers use `.hpp` and sources use `.cpp`.
- Python experiment/report environment: root `pyproject.toml`, `uv.lock`, `.python-version`, and `.venv/`. This is an unpackaged uv environment for scripts, not an installable Python package. Do not add another `pyproject.toml` under `C++/`.
- Preserve Assignment 1's serialized data-frame contract: source MAC 6 bytes, destination MAC 6 bytes, valid unpadded payload length 2 bytes in network order, and sequence number 1 byte. The real header size is 15 bytes despite the PDF's “12 bytes” label.
- Preserve scheme-dependent Checksum-16 and CRC-8/10/16/32 FCS formats, manual serialization, zero padding, and the external two-byte TCP record-length prefix.
- Use one bidirectional TCP connection as a carrier for explicit DATA, ACK, and completion records.
- Use `std::chrono::steady_clock` for RTT and timeout measurement.
- Sequence numbers wrap modulo 256; Selective Repeat must use `N <= 128`, and Go-Back-N may have at most 255 outstanding frames.

## Completed Work

- Inspected the Assignment 1 repository and identified reusable framing, checksum/CRC, error-injection, and socket behavior.
- Read the Assignment 2 PDF and analyzed the Bengali-English professor recording.
- Documented Stop-and-Wait, Go-Back-N, Selective Repeat, channel, timer, ACK, duplicate, buffering, retransmission, and evaluation requirements in `AGENTS.md`.
- Chose C++17 and documented its style, ownership, testing, and build conventions.
- Created the planned `C++/` directory tree with named placeholder headers, sources, tests, tools, fixture directories, result directories, and report location.
- Configured VS Code IntelliSense to use `/usr/bin/g++` and C++17.
- Populated the root and `C++/` `.gitignore` files for Python environments and caches, C++ build products, coverage data, and reconstructed output files while preserving tracked evidence and `.gitkeep` placeholders.
- Removed the duplicate `src/assignment_2/` placeholder package.
- Converted the root uv metadata and lockfile from an installable package to a virtual dependency environment with no console entry point or build backend.
- Removed the remaining `src/` package tree and synchronized `.venv`, uninstalling the former editable package.
- Completed `C++/README.md` with the framing, FCS, TCP-record, and ARQ protocol contracts.
- Added the initial strict C++17 Makefile, compiling all 14 source translation units with `-Wall -Wextra -Wpedantic -Werror`.

## Current Repository State

- `AGENTS.md` contains the contributor and architecture guide.
- `PROGRESS.md` is now the conversation-handoff file.
- `C++/README.md` documents the confirmed wire format and protocol behavior.
- `C++/Makefile` compiles every current source file into ignored object files and provides a focused `test_checksum` target.
- The root and `C++/` `.gitignore` files contain verified project-specific rules.
- `C++/include/checksum.hpp` declares the vector-based Checksum-16 compute API; `C++/src/checksum.cpp` implements even-length behavior, and `C++/tests/test_checksum.cpp` contains even- and odd-length known-vector tests.
- The remaining C++ headers, sources, tests, tools, and report template remain empty placeholders.
- The root `pyproject.toml` and `uv.lock` describe an unpackaged virtual project, no root `src/` package tree remains, and `.venv` is synchronized.
- Git is synchronized with `origin/main` at commit `44f04ec` before this handoff update.
- No sender/receiver executables are linked; the even-length Checksum-16 case passes while the new odd-length case is intentionally RED.

## Current Exact Step

The odd-length Checksum-16 test has reached the expected RED value mismatch. The immediate next step is:

1. treat the final odd byte as the high byte of a zero-padded 16-bit word;
2. fold its carry using the existing rule; and
3. run `make -C 'C++' test_checksum` to confirm both cases are GREEN.

## Recommended Implementation Order

1. Repository ignores, Makefile, Python-tooling layout, and smoke-test target.
2. `config`, checksum, CRC, and reproducible error injection ported from Assignment 1.
3. Data-frame serialization/verification and application record framing.
4. RAII socket, channel simulator, timer/RTT estimator, and metrics.
5. Stop-and-Wait with duplicate and lost-ACK handling.
6. Go-Back-N cumulative acknowledgments and window retransmission.
7. Selective Repeat independent acknowledgments and receiver buffering.
8. Sender/receiver command-line applications and clean end-to-end transfer.
9. Deterministic impairment experiments for probabilities 0.1–0.5.
10. Result validation, plots, report generation, and demonstration rehearsal.

## Current Verification

- The planned C++ module/file manifest is present apart from the intentionally root-level `pyproject.toml`.
- `.vscode/c_cpp_properties.json` parses as JSON and selects `g++` with `cppStandard` set to `c++17`.
- Root `pyproject.toml` parses as TOML.
- `uv lock` resolves one virtual project, and `uv.lock` records `source = { virtual = "." }`.
- `UV_CACHE_DIR=/tmp/assignment2-uv-cache uv sync --check` reports an up-to-date lockfile and `Would make no changes`; `test ! -e src` exits successfully.
- `uv 0.12.5` is installed, and `uv help init` documents that `--no-package` creates a non-importable flat project with no `[build-system]` table.
- `git check-ignore -v .venv/pyvenv.cfg C++/build/example.o C++/output_files/received.bin` confirms that the root virtual environment, C++ build artifacts, and reconstructed output files are ignored.
- `git check-ignore -v C++/build/.gitkeep C++/output_files/.gitkeep` confirms that the placeholder files are explicitly preserved.
- `make -C 'C++' all` successfully compiled all 14 source translation units using `g++`, C++17, and strict warning flags with no warnings or errors.
- Directly compiling `C++/tests/test_checksum.cpp` with `C++/src/checksum.cpp` reached the expected RED linker failure: undefined reference to `flow_control::checksum16_compute(const std::vector<std::uint8_t>&)`.
- `make -C 'C++' test_checksum` reproduced the same expected undefined-reference failure through the permanent focused target.
- After the minimal even-length implementation, `make -C 'C++' test_checksum` compiled cleanly and printed `PASS: even-length checksum`.
- Running `./C++/build/test_checksum` directly also printed `PASS: even-length checksum`.
- After adding the odd-length test, `make -C 'C++' test_checksum` printed `PASS: even-length checksum` followed by the expected RED mismatch `expected 0x97CB, received 0xEDCB` and exited nonzero.
- One of two focused Checksum-16 cases currently passes; do not report a passing target or full-suite count.

## How to Update This File

The coding assistant owns updates to this file; do not ask the user to edit it.

After each meaningful milestone:

- move finished work into **Completed Work**;
- replace **Current Exact Step** with the next concrete action;
- record exact commands and observed results under **Current Verification**;
- update **Current Repository State** and known limitations; and
- change the **Last updated** date.

Keep this file concise and current. Git history preserves old states; this file should describe only the best-known present state.
