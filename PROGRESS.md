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
- Implemented Checksum-16 verification for valid data/checksum pairs using one's-complement folded addition.
- Added mutation-checked coverage that rejects a corrupted payload paired with the original checksum.
- Added mutation-checked coverage that rejects a corrupted received checksum.
- Completed Checksum-16 verification coverage with the valid empty-input checksum boundary.
- Added compile-time frame/FCS configuration constants and a strict header contract target.
- Ported the generic MSB-first CRC engine with CRC-8/10/16/32 parameters, verification, known vectors, empty input, corruption rejection, and invalid-parameter checks.

## Current Repository State

- `AGENTS.md` contains the contributor and architecture guide.
- `PROGRESS.md` is now the conversation-handoff file.
- `C++/README.md` documents the confirmed wire format and protocol behavior.
- `C++/Makefile` compiles every current source file into ignored object files and provides focused checksum and configuration targets.
- `C++/include/config.hpp` owns the 15-byte header, 64–1518-byte frame, FCS-size, 1499-byte maximum-payload, and 46-byte default-payload constants; `check_config_header` enforces them at compile time.
- `C++/include/crc.hpp` and `C++/src/crc.cpp` provide generic widths 1–32 and the four required parameter sets; `test_crc` covers five behavioral groups.
- The root and `C++/` `.gitignore` files contain verified project-specific rules.
- `C++/include/checksum.hpp` declares Checksum-16 computation and verification; `C++/src/checksum.cpp` implements both operations, and `C++/tests/test_checksum.cpp` covers three computation cases plus valid, corrupted-payload, corrupted-checksum, and empty-input verification.
- Error injection, framing, records, channel/timer/metrics/socket, ARQ, applications, tools, and the report remain empty placeholders.
- The root `pyproject.toml` and `uv.lock` describe an unpackaged virtual project, no root `src/` package tree remains, and `.venv` is synchronized.
- No sender/receiver executables are linked; all seven focused Checksum-16 cases pass, but CRC and the remaining project test suite do not exist yet.

## Current Exact Step

Configuration, Checksum-16, and CRC are GREEN. The immediate next step is to add deterministic MSB-indexed bit/burst mutation and RNG/probability tests before implementing error injection.

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
- After adding odd-byte zero padding, `make -C 'C++' test_checksum` compiled cleanly and printed `PASS` for both even- and odd-length checksums.
- After adding the empty-vector boundary test, `make -C 'C++' test_checksum` compiled cleanly and printed `PASS` for even-length, odd-length, and empty-input checksums.
- After implementing valid-pair verification, `timeout 30s env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ test_checksum` exited 0 and printed `PASS` for the three computation cases plus valid checksum verification.
- `timeout 30s env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ all` exited 0 with all strict C++17 translation units current.
- `timeout 5s git diff --check` exited 0.
- With the verifier temporarily mutated to accept every pair, the corrupted-payload test printed `FAIL: corrupted payload was accepted` and the focused target exited nonzero; restoring the implementation made the same target pass.
- The same accept-all mutation made the corrupted-checksum test print `FAIL: corrupted checksum was accepted`; restoring the verifier made it pass.
- With the verifier temporarily mutated to reject every pair, the empty-input verification test printed `FAIL: valid empty-input checksum was rejected`; restoring the verifier made it pass.
- Seven focused Checksum-16 cases pass; do not report a full-suite test count.
- `timeout 30s env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ check_config_header` compiles and runs the configuration contract successfully.
- The first configuration compile caught the invalid assumption that a 46-byte payload itself reaches the 64-byte minimum; the corrected invariant leaves zero padding to frame serialization and constrains the valid payload range.
- `test_crc` first failed to compile because the CRC API was absent, then passed five groups after implementation: parameter constants, four known vectors, empty input, verification, and invalid-parameter rejection.
- `timeout 30s env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ test` exited 0 for configuration, seven checksum cases, and five CRC groups.
- `timeout 30s env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ all` compiled the CRC translation unit with strict warnings and exited 0.

## How to Update This File

The coding assistant owns updates to this file; do not ask the user to edit it.

After each meaningful milestone:

- move finished work into **Completed Work**;
- replace **Current Exact Step** with the next concrete action;
- record exact commands and observed results under **Current Verification**;
- update **Current Repository State** and known limitations; and
- change the **Last updated** date.

Keep this file concise and current. Git history preserves old states; this file should describe only the best-known present state.
