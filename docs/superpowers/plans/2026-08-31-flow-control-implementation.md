# C++17 Data Link Flow Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and verify the complete Assignment 2 C++17 flow-control system and its reproducible evaluation artifacts.

**Architecture:** Pure deterministic protocol state machines are composed with manual frame/record serialization, seeded channel impairment, an adaptive timer, and one move-only TCP connection. Sender and receiver exchange window rounds, allowing Stop-and-Wait, cumulative Go-Back-N, and independently acknowledged Selective Repeat without threads.

**Tech Stack:** C++17, POSIX sockets, GNU Make, Python 3.13 through uv, Python standard library SVG/Markdown generation.

**Spec:** `docs/superpowers/specs/2026-08-31-flow-control-design.md`

## Global Constraints

- Compile with `g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror`.
- Put C++ code in `flow_control`; use two-space indentation and Doxygen comments in public headers.
- Preserve the 15-byte manually serialized header, scheme-dependent FCS, 64–1518-byte frames, and external two-byte TCP record prefix.
- Use modulo-256 sequence numbers, GBN window `<= 255`, and SR window `<= 128`.
- Write each behavior test first, observe RED, implement GREEN, run focused plus existing tests, update `PROGRESS.md`, then commit the coherent slice.
- Do not push commits.

---

### Task 1: Finish Checksum-16 verification

**Files:** `C++/include/checksum.hpp`, `C++/src/checksum.cpp`, `C++/tests/test_checksum.cpp`, `PROGRESS.md`

**Interfaces:** Produces `checksum16_compute(const std::vector<std::uint8_t>&)` and `checksum16_verify(const std::vector<std::uint8_t>&, std::uint16_t)`.

- [ ] Verify the existing valid-checksum test is RED for the missing definition.
- [ ] Implement folded-sum verification and add corrupted-data, corrupted-checksum, and empty-input tests through individual RED/GREEN cycles.
- [ ] Run `make -C C++ test_checksum` and `make -C C++ all`.
- [ ] Commit `feat: complete checksum verification`.

### Task 2: Add configuration, CRC, and deterministic error injection

**Files:** `C++/include/config.hpp`, `C++/include/crc.hpp`, `C++/src/crc.cpp`, `C++/tests/test_crc.cpp`, `C++/include/error_injection.hpp`, `C++/src/error_injection.cpp`, `C++/tests/test_error_injection.cpp`, `C++/Makefile`, `PROGRESS.md`

**Interfaces:** Produces frame constants, `CrcParameters`, four parameter constants, `crc_compute`, `crc_verify`, `ErrorInjectionRng`, deterministic bit/burst operations, and probability selection.

- [ ] Add compile-time configuration assertions and a header syntax target.
- [ ] Add CRC parameter and known-vector tests (`BC`, `199`, `FEE8`, `89A1897F`), observe RED, implement generic widths 1–32, then add verification rejection tests.
- [ ] Add deterministic MSB-indexed mutation and RNG tests, observe RED, implement atomic validation and separate caller-owned RNG state.
- [ ] Run focused targets and `make -C C++ test`.
- [ ] Commit configuration, CRC, and error injection as separate coherent feature commits.

### Task 3: Add protected data frames and application records

**Files:** `C++/include/frame.hpp`, `C++/src/frame.cpp`, `C++/tests/test_frame.cpp`, `C++/include/record.hpp`, `C++/src/record.cpp`, `C++/tests/test_record.cpp`, `C++/Makefile`, `PROGRESS.md`

**Interfaces:** Produces `FcsScheme`, `FrameHeader`, `serialize_frame`, `verify_frame`, `RecordType`, `Record`, `serialize_record`, `parse_record`, ACK integrity helpers, and CONFIG encoding.

- [ ] Add header, size, padding, all-FCS round-trip, malformed, corruption, CRC-10 padding-bit, and short-final-payload tests before implementation.
- [ ] Add record round-trip and malformed-length/type/body tests before implementation.
- [ ] Run `make -C C++ test_frame test_record` and the full suite.
- [ ] Commit frame and record features separately.

### Task 4: Add channel, timer, metrics, and sockets

**Files:** `C++/include/channel.hpp`, `C++/src/channel.cpp`, `C++/tests/test_channel.cpp`, `C++/include/timer.hpp`, `C++/src/timer.cpp`, `C++/tests/test_timer.cpp`, `C++/include/metrics.hpp`, `C++/src/metrics.cpp`, `C++/include/socket.hpp`, `C++/src/socket.cpp`, `C++/tests/test_socket.cpp`, `C++/Makefile`, `PROGRESS.md`

**Interfaces:** Produces `Channel`, `ChannelOutcome`, `TimeoutEstimator`, `Metrics`, move-only `Socket`, exact I/O, listener/connect/accept, and length-prefixed record I/O.

- [ ] Test deterministic clean/drop/delay/corruption outcomes and invalid probabilities before channel implementation.
- [ ] Test initial timeout, EWMA updates, clamps, and ignored retransmitted RTT samples before timer implementation.
- [ ] Test metrics denominators and serialization before metrics implementation.
- [ ] Test socket move ownership, exact record exchange, clean EOF, and truncated record behavior before socket implementation.
- [ ] Run focused targets and the full suite; commit each module slice.

### Task 5: Add the three ARQ state machines

**Files:** `C++/include/protocol.hpp`, `C++/include/stop_and_wait.hpp`, `C++/src/stop_and_wait.cpp`, `C++/tests/test_stop_and_wait.cpp`, `C++/include/go_back_n.hpp`, `C++/src/go_back_n.cpp`, `C++/tests/test_go_back_n.cpp`, `C++/include/selective_repeat.hpp`, `C++/src/selective_repeat.cpp`, `C++/tests/test_selective_repeat.cpp`, `C++/Makefile`, `PROGRESS.md`

**Interfaces:** Produces sender `transmissions/acknowledge/complete` operations and receiver `receive` operations returning ACK, delivery, duplicate, and out-of-order results.

- [ ] Test Stop-and-Wait new send, timeout retransmission, matching/wrong ACK, duplicate receiver delivery, and wraparound before implementation.
- [ ] Test GBN window fill, cumulative ACK slide, whole-window timeout, receiver out-of-order discard, duplicate ACK, max window, and wraparound before implementation.
- [ ] Test SR independent ACK slide, selective retransmission, out-of-order buffering, contiguous delivery, duplicate ACK, max window rejection, and wraparound before implementation.
- [ ] Run all three required protocol targets and the full suite; commit each protocol.

### Task 6: Add sender/receiver sessions and applications

**Files:** `C++/src/sender_main.cpp`, `C++/src/receiver_main.cpp`, `C++/tests/test_end_to_end.py`, `C++/test_data/input.bin`, `C++/Makefile`, `PROGRESS.md`

**Interfaces:** Produces `build/sender` and `build/receiver` CLIs and a single machine-readable sender metrics row.

- [ ] Link the applications from the tested modules and reject malformed CLI/configuration.
- [ ] Add subprocess tests for every protocol/FCS clean transfer, final short frame, deterministic DATA loss/corruption, deterministic ACK loss/corruption, and duplicate-safe output.
- [ ] Run `make -C C++ test_end_to_end` with local socket permission and `make -C C++ test`.
- [ ] Commit application and end-to-end slices.

### Task 7: Add experiments, validation, plots, and report

**Files:** `C++/tools/generate_test_data.py`, `C++/tools/run_experiments.py`, `C++/tools/validate_results.py`, `C++/tools/plot_results.py`, `C++/tools/generate_report.py`, Python tests under `C++/tests/`, `C++/report/report_template.md`, `C++/Makefile`, `C++/README.md`, `PROGRESS.md`

**Interfaces:** Produces deterministic fixtures, `results/experiments.csv`, validated SVG plots, and `report/report.md`.

- [ ] Test fixture determinism and CLI validation.
- [ ] Test experiment identity/counter invariants and probability matrix completeness for 0.0–0.5.
- [ ] Test SVG and report generation from a small fixture before implementation.
- [ ] Run `make -C C++ experiments` and `make -C C++ results` through uv.
- [ ] Commit tooling, evidence, and documentation slices.

### Task 8: Final verification and handoff

**Files:** `PROGRESS.md`, generated evidence files intentionally tracked by repository policy.

- [ ] Run `make -C C++ clean`, `make -C C++ all`, `make -C C++ test`, every required focused target, `make -C C++ experiments`, `make -C C++ results`, `git diff --check`, and inspect Git status.
- [ ] Validate byte-identical end-to-end outputs, experiment identities/denominators, plot XML, and report references.
- [ ] Replace stale handoff state with exact commands, observed counts, limitations, and final commit IDs.
- [ ] Commit `docs: finalize assignment evidence and handoff`.

