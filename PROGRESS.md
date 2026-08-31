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
- The applications run deterministic timeout rounds with no threads and no wall-clock sleeps. A round that produces no window progress is a timeout; it advances a logical completion clock by the current estimator value and retransmits on the next round. Every other round advances that clock by its measured duration with a one-millisecond floor, which keeps RTT samples positive on a loopback carrier.
- The CONFIG record carries one error and one excessive-delay probability per path, so excessive delay models loss: a delayed frame or ACK arrives after the current timeout and is therefore suppressed for its round. The channel's separate drop probability stays zero.
- The sender prints the single metrics row, so its `duplicates` counts acknowledgments that passed their integrity check but moved no window, and its `out_of_order` counts accepted acknowledgments for a frame other than the current window base.
- Stop-and-Wait forces the window to one regardless of `--window`; Go-Back-N rejects anything above 255 and Selective Repeat anything above 128.
- “Efficiency” is defined once, as `EFFICIENCY_DEFINITION` in `C++/tools/validate_results.py`, and imported by the plots and the report: `efficiency = unique_payload_bytes / transmitted_frame_bytes`, the unique delivered payload over every DATA-frame byte the sender put on the wire, including retransmissions, the 15-byte header, zero padding, and the FCS. It matches `flow_control::Metrics::efficiency()`. ACK traffic is not counted, because the sender measures only what it transmits on the DATA path. With a 46-byte payload padded to the 64-byte frame minimum, a retransmission-free run is bounded at 46/64 = 0.71875.
- The experiment matrix is one-factor-at-a-time: 3 protocols x (1 unimpaired baseline + 4 impairment paths x 5 probabilities) = 63 runs. Each run sweeps one of `data-error`, `data-delay`, `ack-error`, `ack-delay` and holds the other three at zero. A full cross product would be 625 combinations per protocol and would confound the paths instead of isolating them.
- Every experiment run is pinned to the same input (`test_data/input.bin`, 4096 bytes), FCS (`crc16`), payload (46 bytes), window (1 for Stop-and-Wait, 8 for Go-Back-N and Selective Repeat), and seed (20260831), so the comparisons are fair.
- A run whose `rtt_sample_count` is zero has an unmeasurable mean RTT: Karn's rule discarded every sample. The validator flags such runs for exclusion from RTT analysis instead of letting `mean_rtt_ms` be read as a real 0 ms. It is not a failure.

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
- Added deterministic MSB-indexed bit/burst mutation, atomic range validation, a reproducible LCG, probability selection, and independently owned RNG state.
- Added canonical protected-frame serialization and verification for the 15-byte header and all five FCS schemes, including scheme-dependent padding and CRC-10 left alignment.
- Added typed CONFIG, DATA, ACK, round-boundary, and completion records with type-specific validation and detectable ACK corruption.
- Added a deterministic application-layer channel with independent drop, excessive-delay, and corruption decisions plus separate selection/bit RNG state.
- Added an adaptive SRTT/RTTVAR timeout estimator with 10--2000 ms clamps and Karn's rule for retransmitted frames.
- Added transfer counters, explicit efficiency/goodput/mean-RTT denominators, zero-denominator handling, and stable CSV serialization.
- Added move-only RAII sockets, exact send/receive loops, TCP listen/connect/accept, and external two-byte application-record framing.
- Implemented the Stop-and-Wait sender/receiver state machine (one outstanding frame, ACK matching, timeout retransmission, duplicate-safe receiver, sequence wraparound), making the pre-existing `test_stop_and_wait` RED suite pass.
- Added the shared `protocol.hpp` `Transmission`/`ReceiveResult` value types used by all three ARQ state machines.
- Wrote `test_go_back_n` first (window fill, cumulative ACK slide, whole-window timeout, duplicate-ACK rejection, invalid-window rejection, sequence wraparound, receiver out-of-order discard, receiver cumulative delivery/duplicate) and implemented Go-Back-N to green.
- Wrote `test_selective_repeat` first (independent ACK slide, selective retransmission, duplicate-ACK rejection, invalid-window rejection, sequence wraparound, receiver out-of-order buffering and contiguous delivery, receiver duplicate ACK) and implemented Selective Repeat to green.
- Wired `test_go_back_n` and `test_selective_repeat` into `C++/Makefile` alongside the existing `test_stop_and_wait` target.
- Added `Socket::receive_framed_record`, which consumes the external prefix and complete body before validation and reports a malformed body instead of throwing, so a simulated corrupted ACK is rejected without desynchronizing the carrier.
- Added shared session helpers for permyriad probability conversion, independently seeded DATA and ACK channel configurations, frame counting, and disabling Nagle's algorithm on the carrier.
- Implemented the sender application: CLI validation, CONFIG handshake, deterministic timeout rounds, DATA-channel impairment, Karn-filtered RTT sampling, and one machine-readable CSV metrics row.
- Implemented the receiver application: CONFIG decoding, FCS verification before delivery, silent discard of rejected frames, contiguous-only file writing with Selective Repeat buffering, ACK-channel impairment, and COMPLETE_ACK closure.
- Added the deterministic 4096-byte `C++/test_data/input.bin` fixture and the `test_end_to_end.py` subprocess suite, and wired `test_end_to_end`, `applications`, and the `sender`/`receiver` link rules into `C++/Makefile`.
- Wrote `test_generate_test_data.py` first (determinism, prefix stability, exact sizing, reproduction of the committed fixture, six CLI rejections, no partial write) and implemented `tools/generate_test_data.py` to green.
- Implemented `tools/run_experiments.py`, which builds the 63-run one-factor-at-a-time matrix, drives `build/receiver` and `build/sender` as subprocesses over loopback TCP exactly as `test_end_to_end.py` does, requires each reconstructed file to be byte-identical to the input, and appends the sender's own metrics row plus matrix metadata to `results/experiments.csv`. Covered by `test_run_experiments.py` for matrix completeness, the one-factor invariant, fixed parameters, window limits, the sender command line, and metrics-row parsing and rejection.
- Wrote `test_validate_results.py` first against a synthetic result set and implemented `tools/validate_results.py` to green: payload-delivery, transmission-counter, wire-byte, acknowledgment, unimpaired-run, denominator, and derived-value identities plus matrix completeness, uniqueness, and fixed-parameter checks.
- Wrote `test_plot_results.py` first and implemented `tools/plot_results.py` to green: 16 hand-rolled standard-library SVG figures (completion time, goodput, efficiency, retransmissions against probability, one figure per impairment path) that validate the results before drawing and carry each marker's source value in `data-run`/`data-value` attributes.
- Wrote `test_generate_report.py` first and implemented `tools/generate_report.py` plus `report/report_template.md` to green, rendering `report/report.md` with every measured number substituted from the validated CSV and every evidence reference relative.
- Added the `experiments`, `results`, `test_tools`, and `fixture` targets to `C++/Makefile`, wired `test_tools` into `make test`, and documented the pipeline and the efficiency definition in `C++/README.md`.
- Task 8: performed a full clean-state rebuild and re-verification of the entire branch. `make -C C++ clean/all/test` and every focused target listed in `AGENTS.md` (`check_config_header`, `test_checksum`, `test_crc`, `test_error_injection`, `test_frame`, `test_record`, `test_channel`, `test_timer`, `test_metrics`, `test_socket`, `test_stop_and_wait`, `test_go_back_n`, `test_selective_repeat`, `test_end_to_end`) all exit 0 individually. Confirmed `test_end_to_end.py` already asserts byte-identical output for all 3 protocols x 5 FCS schemes (15 combinations) in its clean-transfer suite -- no gap, no new test needed. Deleted and regenerated `results/experiments.csv`, all 16 `results/plots/*.svg`, and `report/report.md` from a clean state via `make -C C++ experiments && make -C C++ results`; every regenerated byte matched the previously committed evidence exactly, and `git status` showed no diff. All 16 SVGs parse as well-formed XML via `xml.etree.ElementTree`. `report/report.md` references the CSV and every figure by relative path only, contains no absolute path, and its narrative numbers were spot-checked against the underlying CSV rows and matched. `git diff --check` and `git status` are both clean.
- Final whole-branch review (after Task 8) found one Important issue: `report.md` used Go-Back-N's 12.7x completion-time/goodput jump between DATA-error 0.3 and 0.4 as evidence of "cost compounding," without ever stating that `completion_ms` is a logical clock or that the jump is driven by the timeout estimator sitting at its untrained 100 ms constructor default (Karn's rule discarded every RTT sample at 0.4). Fixed by adding an explicit definition of `completion_ms` to the report and by extending the existing zero-RTT-sample flagging mechanism so affected runs carry the same caveat wherever their completion time or goodput is used, not only their mean RTT. Also fixed four Minor findings from the same review: two interpretive report sentences (Go-Back-N's ACK-path cost, Selective Repeat matching Stop-and-Wait's retransmission count) now derive from the data instead of being asserted as fixed prose; the repeated coincident-series styling explanation is now stated once; and two safety invariants (the receiver's frame-index resolution for Stop-and-Wait/Go-Back-N, and the sender's `sequence_owner_` wraparound reuse) now carry explanatory comments at their point of use. `experiments.csv` and all 16 SVGs are unchanged; only `report.md`, its generator/template, `receiver_main.cpp`, and `sender_main.cpp` changed.

## Current Repository State

- `AGENTS.md` contains the contributor and architecture guide.
- `PROGRESS.md` is now the conversation-handoff file.
- `C++/README.md` documents the confirmed wire format and protocol behavior.
- `C++/Makefile` compiles every current source file into ignored object files and provides focused checksum and configuration targets.
- `C++/include/config.hpp` owns the 15-byte header, 64–1518-byte frame, FCS-size, 1499-byte maximum-payload, and 46-byte default-payload constants; `check_config_header` enforces them at compile time.
- `C++/include/crc.hpp` and `C++/src/crc.cpp` provide generic widths 1–32 and the four required parameter sets; `test_crc` covers five behavioral groups.
- `C++/include/error_injection.hpp` and `C++/src/error_injection.cpp` provide deterministic channel primitives; `test_error_injection` covers five behavioral groups.
- `C++/include/frame.hpp` and `C++/src/frame.cpp` manually serialize fields, zero-pad, compute/encode FCS values, reject malformed or corrupted frames, and return only verified unpadded payloads.
- `C++/include/record.hpp` and `C++/src/record.cpp` encode internal record bytes independently of the external TCP prefix, validate DATA sizes and marker bodies, protect ACKs with a complement byte, and serialize fixed-width session configuration.
- `C++/include/channel.hpp` and `C++/src/channel.cpp` return explicit clean/dropped/delayed/corrupted outcomes and never expose bytes for suppressed transmissions.
- `C++/include/timer.hpp` and `C++/src/timer.cpp` start at 100 ms, update timeout as `SRTT + 4*RTTVAR`, and ignore ambiguous retransmitted samples.
- `C++/include/metrics.hpp` and `C++/src/metrics.cpp` define the required counters and machine-readable derived values.
- `C++/include/socket.hpp` and `C++/src/socket.cpp` close descriptors deterministically, transfer ownership safely, distinguish clean EOF from truncation, and keep the external record prefix outside internal records.
- The root and `C++/` `.gitignore` files contain verified project-specific rules.
- `C++/include/checksum.hpp` declares Checksum-16 computation and verification; `C++/src/checksum.cpp` implements both operations, and `C++/tests/test_checksum.cpp` covers three computation cases plus valid, corrupted-payload, corrupted-checksum, and empty-input verification.
- `C++/include/protocol.hpp` defines the shared `Transmission` and `ReceiveResult` value types used by all three ARQ state machines.
- `C++/include/stop_and_wait.hpp`/`C++/src/stop_and_wait.cpp` implement `StopAndWaitSender`/`StopAndWaitReceiver` with one outstanding frame, ACK matching, timeout retransmission, duplicate-safe delivery, and modulo-256 wraparound.
- `C++/include/go_back_n.hpp`/`C++/src/go_back_n.cpp` implement `GoBackNSender`/`GoBackNReceiver` with sender window `N` (1--255, enforced by `std::invalid_argument`), receiver window 1, cumulative ACKs, whole-window timeout retransmission, and out-of-order discard with re-ACK of the last accepted sequence.
- `C++/include/selective_repeat.hpp`/`C++/src/selective_repeat.cpp` implement `SelectiveRepeatSender`/`SelectiveRepeatReceiver` with both windows `N` (1--128, enforced by `std::invalid_argument`), independent ACKs, selective (per-frame) retransmission, and receiver buffering with contiguous-only delivery.
- `C++/include/session.hpp`/`C++/src/session.cpp` convert probabilities to and from the CONFIG record's permyriad encoding, derive independently seeded DATA and ACK channel configurations, count frames, and disable Nagle's algorithm on the carrier.
- `C++/src/sender_main.cpp` links `build/sender`: it validates the CLI, sends CONFIG, runs deterministic timeout rounds, applies the DATA channel, measures Karn-filtered RTT on a logical clock, and prints one CSV metrics row.
- `C++/src/receiver_main.cpp` links `build/receiver`: it accepts one connection, decodes CONFIG, verifies every frame before delivery, writes only contiguous in-order payload, applies the ACK channel, and answers COMPLETE with COMPLETE_ACK.
- `C++/test_data/input.bin` is a tracked 4096-byte deterministic fixture; `C++/tests/test_end_to_end.py` drives both binaries as subprocesses.
- `C++/tools/generate_test_data.py` regenerates that fixture as a pure function of `--seed` and `--size`; `C++/tools/run_experiments.py` runs the 63-run matrix; `C++/tools/validate_results.py` gates the pipeline on identities and denominators and owns the single efficiency definition; `C++/tools/plot_results.py` renders 16 SVG figures with the standard library only; `C++/tools/generate_report.py` renders the report from `C++/report/report_template.md`.
- `C++/tests/` additionally holds `test_generate_test_data.py`, `test_run_experiments.py`, `test_validate_results.py`, `test_plot_results.py`, and `test_generate_report.py`, all plain scripts with explicit pass/fail lines and exit status, matching `test_end_to_end.py`; no test framework was added.
- Generated evidence is committed: `C++/results/experiments.csv` (63 rows), `C++/results/plots/` (16 SVG figures), and `C++/report/report.md`.
- `C++/Makefile` provides `experiments`, `results`, `test_tools`, and `fixture` alongside the existing build and test targets; `C++/README.md` documents the pipeline, the matrix design, and the efficiency definition.
- The root `pyproject.toml` and `uv.lock` describe an unpackaged virtual project with no dependencies, no root `src/` package tree remains, and `.venv` is synchronized. All Python tooling is standard library only and is invoked through `uv run`.
- `build/sender` and `build/receiver` link and complete byte-identical transfers; all unit-level, end-to-end, and tooling tests pass.

## Current Exact Step

Implementation and review are complete. All 8 planned tasks are done and the final whole-branch review (following Task 8) returned "ready to merge with fixes"; its one Important and four Minor findings were fixed in a single fix wave, re-reviewed clean, and one further Minor (missing dedicated test coverage for the new report-caveat logic) was parked rather than triggering another review round. Every C++ module, the three ARQ protocols, the sender/receiver applications, and the full Python experiment/validation/plotting/report pipeline exist, are GREEN on a clean rebuild (194/194 tests), and the committed evidence (`results/experiments.csv`, all 16 SVG plots, `report/report.md`) reproduces byte-identically from a clean state. There is no more implementation work planned on this branch. Remaining work is outside the scope of code changes: report/demo preparation for the 31 August-4 September submission window, and finishing the development branch (merge or PR, per the user's choice).

## Known Limitations

- The sender's CLI exposes one error probability and one excessive-delay probability per path; there is no separate drop flag, because excessive delay is how this system models loss. The `data-delay` and `ack-delay` sweeps are therefore the loss sweeps.
- Because `select_probability` consumes no random draw when a probability is exactly 0.0, a one-factor sweep of `data-error` at probability *p* and a sweep of `data-delay` at the same *p* select exactly the same transmissions from the same channel decision stream. A corrupted DATA frame and a suppressed DATA frame are also indistinguishable in the sender's metrics: the receiver acknowledges neither, and wire bytes are counted before the channel decides. The two DATA sweeps therefore produce identical rows, as do the two ACK sweeps. This is explained in the report; it is a property of the model, not a defect.
- The experiment matrix fixes one FCS scheme (`crc16`) and varies the protocol. Byte-exactness under all five schemes is covered separately by `test_end_to_end.py`, not by the matrix.
- Completion time is a logical clock driven by deterministic rounds, not wall-clock time on a physical link, so absolute milliseconds are only meaningful relative to one another.

The following were raised as Minor findings during Task 5-7 code review, deliberately parked as non-blocking rather than fixed, and are recorded here so the branch's own handoff document names them honestly instead of presenting the branch as flawless. A final whole-branch review will see these too:

- **Task 5** (Stop-and-Wait/Go-Back-N/Selective Repeat): the Go-Back-N receiver only special-cases a duplicate of the immediately preceding sequence number, not older duplicates; there is an ACK-on-out-of-order asymmetry between Stop-and-Wait and Go-Back-N. Both pre-existing, not touched by the task that found them.
- **Task 6** (applications/end-to-end tests):
  - ACK corruption is exercised end-to-end only for Selective Repeat, not for Go-Back-N's cumulative-ACK case.
  - The sender keeps a shadow window-fill copy of state for Go-Back-N metrics (`sender_main.cpp`, ~lines 562-618) that duplicates state already tracked inside `go_back_n.cpp`; correct today, but a duplication risk if that module's semantics change later.
  - `--window` silently defaults to 1 for Go-Back-N/Selective Repeat when omitted (easy to mistake for Stop-and-Wait behavior on a forgotten flag), and Stop-and-Wait silently discards a user-supplied `--window` rather than warning.
  - A dead validation branch exists in `parse_unsigned`, and a redundant floor exists in `elapsed_ms_since`.
  - A failed `setsockopt(TCP_NODELAY)` is silently swallowed, even though Nagle's algorithm materially affects measured RTT (87 ms vs 1 ms was observed in this project).
  - `test_end_to_end.py`'s readiness handshake greps the receiver's human-readable log string (`"listening"`); a wording change to that message would hang the test for 120 seconds instead of failing fast.
  - The Makefile's `applications` target recompiles the 13 shared source files twice more (once per binary) instead of reusing the existing `%.o` pattern rule -- correct output, just roughly 3x the necessary build work.
- **Task 7** (experiment/validation/plot/report tooling): the zero-coincidence fallback branch in `generate_report.py`'s `render_path_coincidences` (the case where no impairment-path pair coincides) is never exercised by a test, because the synthetic fixture in `test_generate_report.py` always yields 3+ coincident pairs. Low-risk (a one-line untested string-return branch), but genuinely untested.
- **Final whole-branch review fix wave**: the new completion-time-caveat and data-derived-comparison functions added to `generate_report.py` (`completion_estimator_caveat`, `gbn_ack_path_retransmission_free`, `sr_matches_stop_and_wait_retransmissions`) ship without dedicated new test cases asserting on their exact output; existing tests incidentally exercise them but don't check the new sentences. Two independent reviewers traced the logic and confirmed it is genuinely data-derived and would adapt correctly to a different flagged run, so the risk is a silent regression on a future edit, not a present defect.

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
- `test_error_injection` first failed to compile because its API was absent, then passed five groups after implementation: MSB bit flips, atomic bursts, reproducible RNG, probability selection, and independent RNG state.
- `test_frame` first failed to compile because its API was absent, then passed eight groups: manual header layout, scheme padding, all-FCS round trips, malformed/corruption rejection, CRC-10 alignment, short final payload, invalid serialization, and the 1499-byte CRC-32 boundary.
- Mutating the maximum-payload comparison to reject 1499 bytes made the boundary test abort nonzero; restoring the comparison made the target pass.
- `test_record` first failed to compile because its API was absent, then passed four groups: typed round trips, ACK complement integrity, CONFIG round trip, and malformed rejection.
- `test_channel` first failed to compile because its API was absent, then passed forced outcomes, reproducible corruption, empty corruption handling, and invalid probability rejection.
- `test_timer` first failed to compile because its API was absent, then passed initial/first sample, EWMA update, clamp, Karn-rule, and invalid-sample behavior in four groups.
- `test_metrics` first failed to compile because its API was absent, then passed zero denominators, derived calculations, and stable CSV output.
- `test_socket` first failed to compile because its API was absent. Its first sandboxed run was blocked at `send` with `Operation not permitted`; the identical permission-capable run passed move ownership, exact record exchange, clean EOF, and truncated-record rejection.
- The current full `make -C C++ test` exits 0 with local socket permission for configuration plus 44 printed unit-test groups, and `make -C C++ all` compiles all implemented translation units with strict warnings.
- `make -C C++ test_stop_and_wait` compiled and passed all five cases against the pre-existing test file once `protocol.hpp`/`stop_and_wait.hpp`/`stop_and_wait.cpp` were implemented (previously empty, so linking failed with undefined references — the expected RED).
- `test_go_back_n` and `test_selective_repeat` were written first and confirmed RED (undefined-reference link failures against empty `.cpp` files), then each protocol's implementation made its own eight/seven cases pass.
- `make -C C++ test` exits 0 with all 65 printed test groups across every module (checksum, config, CRC, error injection, frame, record, channel, timer, metrics, socket, Stop-and-Wait, Go-Back-N, Selective Repeat) passing with pristine output.
- `C++/test_data/input.bin` was generated by `python3 -c "import random; open('C++/test_data/input.bin','wb').write(random.Random(20260831).randbytes(4096))"` and has SHA-256 `01a92fd854772cfce24733637bb3c01f3a63a9c25b283c378c9e7e8802ce3ca7`. 4096 is not a multiple of 46 or 100 and is an exact multiple of 64, so both final-frame shapes are covered.
- The first clean Stop-and-Wait transfer took 8.3 s because Nagle's algorithm held each round's second small write until the first was acknowledged; disabling it on both endpoints reduced the same transfer to well under a second and made the mean RTT 1 ms instead of 87 ms.
- `make -C C++ test_end_to_end` exits 0 with local socket permission and prints 48 `PASS` lines: 15 clean protocol/FCS transfers, four payload-division cases including the 1499-byte CRC-32 boundary, nine deterministic DATA-loss/DATA-corruption/ACK-loss cases, one ACK-corruption case, three duplicate-safe heavy-impairment cases, one seeded-replay case, and 15 CLI rejection cases.
- Mutation checks confirmed the end-to-end suite is not vacuous: forcing the receiver to verify every frame with Checksum-16 regardless of the negotiated scheme made all 19 non-Checksum-16 cases fail with `sender: transfer did not converge within its round budget`, and writing each delivered payload twice made 33 cases fail with `output file is not byte-identical to the input`. Restoring the receiver made all 48 cases pass again.
- Manual worst-case runs with `--data-error 0.5 --data-delay 0.5 --ack-error 0.5 --ack-delay 0.5` reproduced the input byte-for-byte for Stop-and-Wait, Go-Back-N with window 32, and Selective Repeat with window 128, using 1455, 279, and 80 timeouts respectively against a 19 000-round budget.
- A Stop-and-Wait run with `--window 8` still reported 90 original transmissions and no retransmissions, confirming the forced window of one; an empty input file completed with a zero-byte output.
- `make -C C++ all` compiles every translation unit and links `build/sender` and `build/receiver` with `-Wall -Wextra -Wpedantic -Werror` and no warnings; the target exits 0.
- Each new Python tool was confirmed RED before implementation: `test_generate_test_data.py` failed all 12 cases against the empty placeholder, and `test_run_experiments.py`, `test_plot_results.py`, and `test_generate_report.py` each aborted with `AttributeError` on the first symbol they needed. `test_validate_results.py` failed 24 of its 26 cases (the two that "passed" were vacuous against a validator that always exits 0).
- `uv run python C++/tools/generate_test_data.py --seed 20260831 --size 4096` reproduces `C++/test_data/input.bin` byte for byte; its SHA-256 remains `01a92fd854772cfce24733637bb3c01f3a63a9c25b283c378c9e7e8802ce3ca7`.
- `make -C C++ experiments` exits 0 and writes 63 rows to `C++/results/experiments.csv` in about 1.1 s; every run's reconstructed file was byte-identical to the input, or the matrix would have aborted.
- `make -C C++ results` exits 0: validation reports `63 runs satisfy every identity`, plotting writes 16 SVG figures, and the report renders 63 runs. Validation additionally prints four `FLAG:` lines for `go-back-n__data-error__0.4/0.5` and `go-back-n__data-delay__0.4/0.5`, whose mean RTT is not measurable because Karn's rule discarded every sample.
- Deleting `results/experiments.csv`, `results/plots/*.svg`, and `report/report.md` and rerunning `make -C C++ experiments && make -C C++ results` regenerated all 18 files byte-identically to the committed versions: `git status --short` afterwards listed no change to any generated file.
- Mutation-tested the validator against the real results: incrementing `retransmissions` by one in the `go-back-n__data-error__0.3` row of a copy of `results/experiments.csv` made `validate_results.py` exit 1 with `VIOLATION: go-back-n__data-error__0.3: transmitted_frame_bytes 31104 does not equal 487 frames x 64 wire bytes = 31168`.
- `test_validate_results.py` applies 16 further single-field mutations to a synthetic result set -- payload above the input size, an unimpaired run that lost payload, too few original transmissions, a negative counter, a zero completion time, a zero efficiency denominator, an efficiency/goodput/mean-RTT value contradicting its own definition, a nonzero mean with no samples, mismatched wire bytes, more duplicates than ACKs, an unimpaired run that retransmitted or timed out, a zero timeout, and a non-numeric counter -- and requires each to be rejected by run name. It also rejects an incomplete probability matrix, a missing protocol, a duplicated run, a wrong header, an empty file, and a missing file.
- Every generated SVG parses with `xml.etree.ElementTree`, has an `svg` root in the SVG namespace with `viewBox`, `width`, `height`, `<title>`, and `<desc>`, and carries each marker's exact CSV value in `data-run`/`data-value`; `test_plot_results.py` compares all 18 markers of all 16 figures against the source rows. Rendering one figure to PNG with `inkscape` confirmed the axes, log scale, legend, and series are legible.
- Coincident series were caught during self-review: Stop-and-Wait and Selective Repeat produce identical efficiency and retransmission curves under DATA-path impairment, and the blue Stop-and-Wait line was completely hidden. The figures now distinguish series by dash pattern and marker shape as well as colour, with Selective Repeat drawn as an open ring over the marker it coincides with.
- `report/report.md` contains no absolute or machine-specific path and no timestamp; rendering the same results twice is byte-identical, and `test_generate_report.py` asserts both.
- `make -C C++ test` exits 0 with 191 printed `PASS` lines and no `FAIL` line: 65 unit-test groups, 48 end-to-end cases, and 78 tooling cases (12 fixture, 16 experiment-matrix, 26 validation, 11 plot, 13 report).
- `git diff --check` exits 0.

### Task 8 final verification (clean-state re-run, all commands run and observed, none assumed)

- `env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ clean` exits 0 and removes every build artifact.
- `env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ all` exits 0 from a clean tree, compiling all 15 translation units with `-Wall -Wextra -Wpedantic -Werror` and linking `build/sender`/`build/receiver` with no warnings; a second run reports `Nothing to be done for 'all'`, exit 0.
- `env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ test` exits 0 and prints 194 `PASS` lines (three more than the 191 recorded after Task 7, from the fix-round-1 ACK-path-coincidence tests added to `test_generate_report.py`); no `FAIL` line.
- Each focused target run individually -- `check_config_header`, `test_checksum` (7), `test_crc` (5), `test_error_injection` (5), `test_frame` (8), `test_record` (4), `test_channel` (4), `test_timer` (4), `test_metrics` (3), `test_socket` (4), `test_stop_and_wait` (5), `test_go_back_n` (8), `test_selective_repeat` (8), `test_end_to_end` (48) -- all exit 0 with the expected PASS counts (parenthesized), matching `make test`'s aggregate.
- Read `C++/tests/test_end_to_end.py`: its `clean_transfer` case iterates `for protocol in PROTOCOLS: for fcs in FCS_SCHEMES:` (3 protocols x 5 FCS schemes = 15 cases) and asserts `received == expected` (byte-identical to the input) plus zero retransmissions/timeouts for each. This already satisfies "byte-identical for every protocol and FCS scheme"; no gap, no new test was needed.
- Backed up `results/experiments.csv`, `results/plots/*.svg`, and `report/report.md`, deleted them, then ran `env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ experiments` (exits 0, writes 63 rows) followed by `env -u MAKEFLAGS -u MFLAGS /usr/bin/make -C C++ results` (exits 0: `validate_results.py` reports `63 runs satisfy every identity` plus the same 4 expected `FLAG:` lines for Go-Back-N runs with zero RTT samples, `plot_results.py` writes 16 SVG figures, `generate_report.py` reports `63 runs reported`). `diff -q`/`diff -qr` against the backups showed the regenerated CSV, all 16 SVGs, and the report are byte-for-byte identical to what was committed; `git status --short` showed no change.
- `python3 -c "import xml.etree.ElementTree as ET; ET.parse(path)"` succeeded for all 16 files under `C++/results/plots/*.svg` -- all well-formed XML.
- Confirmed `C++/report/report.md` exists, references `../results/experiments.csv` and every `../results/plots/*.svg` figure by relative path only (no absolute or machine-specific path anywhere in the file), and its Section 4 per-path tables and Section 6 complete result set were spot-checked against the regenerated `experiments.csv` and matched exactly.
- `git diff --check` exits 0 (no whitespace errors).
- `git status` after every step above reports a clean working tree with nothing to commit -- the clean rebuild and full pipeline rerun produced no unexpected untracked or modified files.
- Read the SDD ledger (`.superpowers/sdd/2026-08-31-flow-control-implementation/progress.md`) in full and folded its parked Task 5-7 Minor findings into Known Limitations above, rather than silently dropping them from the handoff.
- Final commit for this task: `docs: finalize assignment evidence and handoff` (PROGRESS.md only -- the clean-state regeneration of `experiments.csv`/SVGs/`report.md` produced no byte-level change, so no evidence files were re-committed).

## How to Update This File

The coding assistant owns updates to this file; do not ask the user to edit it.

After each meaningful milestone:

- move finished work into **Completed Work**;
- replace **Current Exact Step** with the next concrete action;
- record exact commands and observed results under **Current Verification**;
- update **Current Repository State** and known limitations; and
- change the **Last updated** date.

Keep this file concise and current. Git history preserves old states; this file should describe only the best-known present state.
