# Repository Guidelines

## Assignment Scope

This repository implements Data Link Layer flow control in a simulated network using C++17. Reuse the behavior and wire contract of Assignment 1's C17 framing, checksum/CRC, error-injection, and TCP socket modules, then add Stop-and-Wait, Go-Back-N ARQ, and Selective Repeat ARQ. Keep Assignment 1 unchanged; port or adapt reusable modules into this repository rather than editing the previous assignment.

The demonstration is due 24–28 August 2026. The report is due 31 August–4 September 2026, with a soft copy uploaded to the course drive.

## Project Structure & Module Organization

The repository root contains the assignment PDF, professor recording, `README.md`, `LICENSE`, `PROGRESS.md`, and the shared Python-tooling files `pyproject.toml`, `uv.lock`, and `.python-version`. Put the active C++17 implementation under `C++/`:

- `C++/include/`: public `.hpp` interfaces, types, and protocol constants.
- `C++/src/`: `.cpp` framing, channel, timer, ARQ, sender, receiver, and socket implementations.
- `C++/tests/`: `test_<module>.cpp` unit tests and end-to-end tests.
- `C++/test_data/`: small deterministic fixtures.
- `C++/input_files/` and `C++/output_files/`: experiment inputs and received files.
- `C++/tools/`: reproducible experiment, validation, plotting, and report scripts.
- `C++/results/` and `C++/report/`: generated evidence and written analysis.
- `C++/build/`: ignored binaries and object files.

The root `pyproject.toml` and `uv.lock` define the Python environment used by experiment, validation, plotting, and report utilities. Keep the root `.venv/` ignored and run Python tools through `uv`; do not create a second `pyproject.toml` under `C++/`. Keep any future C, Java, or standalone Python implementations in separate top-level directories with independent build files.

## Reused Assignment 1 Wire Contract

Preserve the logical data-frame header:

| Field | Size |
| --- | ---: |
| Source MAC | 6 bytes |
| Destination MAC | 6 bytes |
| Valid payload length | 2 bytes, network byte order |
| Sequence number | 1 byte |

The header is therefore **15 bytes**, despite the Assignment 2 diagram's incorrect “12 bytes” label. Represent it with a logical C++ type, but serialize each field individually; never copy an object's in-memory representation to the wire. The length is the actual unpadded payload length, not the frame length.

The FCS covers the serialized header and complete padded payload, excluding the FCS itself. Retain selectable Assignment 1 schemes: Checksum-16, CRC-8 `0xD5`, CRC-10 `0x233`, CRC-16 `0x8005`, and CRC-32 `0x04C11DB7`. FCS sizes remain scheme-dependent (1, 2, or 4 bytes), even though the PDF diagram shows 4 bytes.

Complete simulated frames remain 64–1518 bytes. The PDF's 1500-byte payload plus a 15-byte header and 4-byte CRC would be 1519 bytes; use at most 1499 bytes when CRC-32 is enabled. A fixed 46-byte payload, as suggested by the professor, is safe for every scheme. Preserve the true length of a short final file chunk and zero-pad only for transmission.

TCP is only the carrier. Prefix each application record with an external two-byte network-order record length so corrupted simulated headers cannot desynchronize the stream. Do not include or corrupt this prefix as part of a data frame.

## Architecture & Protocol Behavior

Use one bidirectional socket connection managed by an RAII socket wrapper. Model DATA, ACK, and transfer-completion records explicitly. TCP reliability must not replace the assignment's application-layer loss, corruption, timeout, or retransmission behavior.

The channel must reproducibly apply configured probabilities to both the data and ACK paths. Support bit corruption through the Assignment 1 module and random excessive delay/loss. A frame or ACK that misses its timeout is treated as lost. Seed frame selection separately from error-position selection so experiments can be repeated fairly.

Use `std::chrono::steady_clock` for timers and RTT measurements. Start timing when a frame is transmitted. Recompute the timeout from valid recent RTT samples, document the chosen estimator and safety margin, and avoid ambiguous RTT samples from retransmitted frames.

Receiver rules common to all protocols:

- Verify structure and FCS before accepting a data frame.
- Discard corrupted frames and send no success ACK for them.
- Detect retransmitted duplicates, never write their payload twice, and resend the appropriate ACK when needed.

Protocol-specific rules:

- **Stop-and-Wait:** keep one outstanding frame; send the next only after its matching ACK; retransmit the same frame after timeout.
- **Go-Back-N:** sender window is `N`, receiver window is 1, ACKs are cumulative, and the receiver discards out-of-order frames. On timeout, retransmit all outstanding frames from the window base.
- **Selective Repeat:** both windows are `N`; ACKs are independent; the receiver buffers valid out-of-order frames and delivers them only when contiguous. Retransmit only unacknowledged frames.

Sequence numbers are one byte and wrap modulo 256. Limit Go-Back-N to at most 255 outstanding frames and Selective Repeat to `N <= 128` to avoid ambiguous wrapped sequence numbers.

## Build, Test, and Development Commands

When the C++ scaffold is added, preserve a simple root-invoked interface:

```bash
make -C C++ all                 # strict C++17 build
make -C C++ test                # all unit and validation tests
make -C C++ test_stop_and_wait  # protocol-specific tests
make -C C++ test_go_back_n
make -C C++ test_selective_repeat
make -C C++ test_end_to_end     # sender/receiver transfer checks
make -C C++ experiments         # reproducible experiment matrix
make -C C++ results             # validate data and build plots/report
make -C C++ clean               # remove generated build output
```

Compile with `g++` and `-std=c++17 -Wall -Wextra -Wpedantic -Werror`. Do not claim a command or target works until it exists and has been run successfully.

## Coding Style & Naming Conventions

Use two-space indentation and place opening braces on a new line for namespaces, classes, and functions. Use `snake_case` for files, functions, and variables; `PascalCase` for classes and scoped enums; and `UPPER_SNAKE_CASE` for compile-time protocol constants. Put project code in a `flow_control` namespace and use fixed-width types such as `std::uint8_t` for wire data.

Prefer RAII and value types: `std::array` for fixed wire fields, `std::vector` for variable buffers, smart pointers only when ownership cannot remain direct, and standard streams for files. Do not use raw `new`/`delete`. A socket wrapper must close its descriptor in its destructor and define safe move behavior. Public `.hpp` files require Doxygen `/** ... */` comments. Keep protocol state machines separate from socket I/O and channel simulation so each can be tested deterministically.

## Testing and Evaluation Guidelines

Use small C++ test executables with assertions or explicit pass/fail status; do not add a test framework unless it provides clear value. Cover normal, boundary, malformed, wraparound, duplicate, lost-data, lost-ACK, corrupted-data, corrupted-ACK, delayed, timeout, move/ownership, and final-short-frame cases. Pin RNG seeds and simulated events; rejected operations must not partially modify state.

End-to-end clean runs must reproduce input byte-for-byte for every protocol and FCS scheme. Compare all three protocols with no impairment and with probabilities 0.1–0.5 for errors or delays affecting data or ACKs. Use the same input, payload size, window sizes, seeds, and impairment schedule across comparisons.

Record at least completion time, goodput, unique payload delivered, original transmissions, retransmissions, ACKs, timeouts, duplicates, out-of-order events, RTT, and timeout values. Define “efficiency” explicitly before interpreting results. Validate experiment identities and denominators before generating tables or graphs.

## Progress Tracking and Conversation Handoff

`PROGRESS.md` is the authoritative current-state handoff. At the beginning of each new conversation, read both `AGENTS.md` and `PROGRESS.md`, then inspect the working tree before proposing work. After every meaningful milestone, update `PROGRESS.md` with the date, completed work, exact verification commands and results, current step, known limitations, and immediate next step. Replace stale status rather than accumulating contradictory history. Never describe an unrun build or test as passing.

## Commit & Pull Request Guidelines

Follow the existing short, imperative style, for example `feat: add stop-and-wait sender state`. Keep commits scoped to one logical change. Pull requests should summarize behavior, identify affected modules, list exact test commands and results, and explain protocol or wire-format decisions. Include plots or screenshots only when experiment or report presentation changes.

## Agent-Specific Instructions

Treat this as a guided C++17 learning project. Do not implement C++ code unless the user explicitly requests it. Normally provide one small user-entered step at a time, explaining what it does, why it is needed, how it integrates, the exact test command, and expected output. A request such as “do the rest yourself” or “fix these” grants authority only for that stated task. Preserve unrelated user changes and distinguish verified behavior from assumptions, especially where the PDF, recording, and implementation differ. Keep `PROGRESS.md` synchronized whenever the verified project state or next step changes.
