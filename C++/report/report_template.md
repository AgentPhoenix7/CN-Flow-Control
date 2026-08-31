# Data Link Layer Flow Control: Stop-and-Wait, Go-Back-N, and Selective Repeat

> This file is generated. Do not edit `report.md` by hand: edit
> `report_template.md` and re-run `make -C C++ results`.

This report compares three Automatic Repeat reQuest (ARQ) protocols implemented
in C++17 over a simulated Data Link Layer. All numbers below come from
[`../results/experiments.csv`](../results/experiments.csv), which is produced by
`tools/run_experiments.py` and checked by `tools/validate_results.py` before any
figure or table in this report is rendered.

## 1. The protocols

All three protocols move a file across one bidirectional TCP connection that
acts only as a carrier. Loss, corruption, delay, acknowledgment, timeout,
duplicate detection, and retransmission are all simulated at the application
layer; TCP's own reliability is never allowed to stand in for them.

**Stop-and-Wait** keeps exactly one frame outstanding. The sender transmits a
frame and advances only when the matching acknowledgment arrives; on timeout it
retransmits the same frame. It is the simplest protocol and wastes the most time
waiting, but it never retransmits a frame that was already delivered.

**Go-Back-N** gives the sender a window of `N` frames and the receiver a window
of one. Acknowledgments are cumulative, so an acknowledgment for frame `k`
implicitly acknowledges everything before it. The receiver discards any frame
that arrives out of order. On timeout the sender retransmits every outstanding
frame from the window base, which is what makes a single lost frame expensive:
the whole window is resent behind it.

**Selective Repeat** gives both sender and receiver a window of `N`.
Acknowledgments are independent, the receiver buffers valid out-of-order frames
and delivers them only once they are contiguous, and the sender retransmits only
the frames that were actually not acknowledged. It costs receiver memory and
bookkeeping in exchange for retransmitting far less.

## 2. The wire contract

Every data frame carries a manually serialized 15-byte header, namely a 6-byte
source MAC address, a 6-byte destination MAC address, a 2-byte valid payload
length in network byte order, and a 1-byte sequence number, followed by the
padded payload and a Frame Check Sequence. No object's in-memory representation
is ever copied to the wire.

The length field records the *unpadded* payload length, so a short final chunk is
reconstructed exactly and zero padding is never written to the output file. A
complete simulated frame is between 64 and 1518 bytes; short frames are padded up
to the 64-byte minimum for transmission.

The FCS covers the serialized header and the complete padded payload, excluding
itself, and its size depends on the selected scheme: Checksum-16 (2 bytes), CRC-8
`0xD5` (1 byte), CRC-10 `0x233` (2 bytes), CRC-16 `0x8005` (2 bytes), and CRC-32
`0x04C11DB7` (4 bytes). The receiver verifies structure and FCS before it will
accept a frame; a frame that fails is discarded silently and is never
acknowledged, not even negatively.

Sequence numbers are one byte and wrap modulo 256, so Go-Back-N is limited to at
most 255 outstanding frames and Selective Repeat to `N <= 128`, which keeps a
wrapped sequence number unambiguous.

Each application record on the TCP stream carries an external two-byte
network-order length prefix. That prefix is transport framing: it is not part of
a simulated frame, is not covered by the FCS, and is never chosen for simulated
corruption, so a corrupted simulated header can never desynchronize the byte
stream.

## 3. Methodology

### 3.1 What was varied, and what was held fixed

{{MATRIX_SUMMARY}}

The experiment is a **one-factor-at-a-time** design. The evaluation guideline
asks for all three protocols with no impairment and with probabilities 0.1
through 0.5 for errors and for delays, independently affecting the DATA and ACK
paths. Each run therefore holds three of the four impairment probabilities at
zero and sweeps the fourth. A full cross product of four dimensions at five
levels would be 625 combinations per protocol and would confound the paths with
one another; sweeping them independently is what isolates each path's effect,
which is the comparison the guideline is asking for.

Everything not being varied is pinned so that the columns are comparable:

{{MATRIX_PARAMETERS}}

### 3.2 How loss is modelled

The sender exposes one *error* probability and one *excessive delay* probability
per path. There is no separate drop probability, because excessive delay is how
this system models loss: a frame or acknowledgment that is delayed never arrives
before its timeout expires, so it is suppressed for that round and the sender
must recover exactly as it would from a loss. The `data-delay` and `ack-delay`
sweeps are therefore the loss sweeps, and the `data-error` and `ack-error` sweeps
are the bit-corruption sweeps.

### 3.3 Efficiency, defined before it is interpreted

{{EFFICIENCY_DEFINITION}}

This is the definition used by `flow_control::Metrics::efficiency()` in
`src/metrics.cpp`, by `tools/validate_results.py`, by every efficiency figure,
and by every efficiency number in this report. It is stated once, in
`EFFICIENCY_DEFINITION`, and imported everywhere else.

Note that the baseline efficiency is bounded well below 1.0 by framing overhead
alone. With a 46-byte payload the frame is padded to the 64-byte minimum, so a
transfer with no retransmission at all can reach at most 46/64 = 0.71875.

### 3.4 Completion time, defined before it is interpreted

`completion_ms` is the simulation's logical clock: the sum of every round's
measured duration, each floored at a minimum of 1 ms, plus -- for every round
in which no progress was made -- the *current* value of the adaptive timeout
estimator at that point in the run. That estimator
(`include/timer.hpp`/`src/timer.cpp`) starts at a 100 ms constructor default
and adapts as RTT samples arrive, but Karn's rule discards the RTT sample of
any frame that was retransmitted, so a run that times out on every
progress-bearing frame never collects a sample and pays every one of its
timeout rounds at the unadapted 100 ms default rather than a value the
estimator ever fitted to this run's channel.

The estimator's value at any point in a run is therefore a separate,
data-dependent quantity driven by that run's own RTT samples, not a fixed
simulation parameter like the payload size or window. A large jump in
`completion_ms` between two runs can reflect a change in this estimator floor
as much as a change in retransmission count, so it should not be read as
protocol cost alone without checking `current_timeout_ms` and
`rtt_sample_count` for the runs being compared. §3.5 explains how a run with
zero RTT samples is detected and disclosed; the same runs are called out again
in §4.3 wherever their completion time or goodput is discussed.

### 3.5 Validation before interpretation

`tools/validate_results.py` runs before any figure or table is produced and fails
the whole pipeline, naming the offending run, if any of the following does not
hold:

- the delivered unique payload equals the input file size;
- `original_transmissions` equals the number of frames the input requires,
  `ceil(input_bytes / payload_bytes)`;
- every counter is a non-negative integer;
- `transmitted_frame_bytes` equals the total number of transmissions times the
  serialized wire size of one frame;
- `duplicates` does not exceed `acks`, and `out_of_order` does not exceed the
  acknowledgments that actually moved the window;
- an unimpaired run reports no retransmission, timeout, or duplicate;
- `completion_ms`, `transmitted_frame_bytes`, and `current_timeout_ms` are all
  strictly positive, so no derived value is ever computed over a zero
  denominator; and
- `efficiency`, `goodput_bytes_per_second`, and `mean_rtt_ms` each agree with
  their own definitions to within the six decimals the sender prints.

Matrix completeness is checked too: every protocol must have a baseline run and
all five probability levels on all four impairment paths, no run may appear
twice, and the fixed parameters must genuinely be constant across the runs being
compared.

A run whose `rtt_sample_count` is zero is a special case. Karn's rule discards
the RTT sample of any frame that was retransmitted, so under heavy DATA-path
impairment Go-Back-N can finish without a single unambiguous sample. Its
`mean_rtt_ms` denominator is zero and the mean is *not measurable*. Such runs are
flagged and excluded from RTT interpretation rather than being read as 0 ms.

{{FLAGGED_RUNS}}

### 3.6 Test strategy for the tooling

The validation, plotting, and report tooling was written test-first against a
small synthetic result set whose values are known exactly:

- `tests/test_generate_test_data.py` requires the fixture generator to be a pure
  function of its seed and size, to reproduce the committed
  `test_data/input.bin` byte for byte, and to reject invalid command lines
  without writing a partial file.
- `tests/test_run_experiments.py` covers what is unit-testable in the runner
  without mocking real subprocesses: matrix completeness for 0.1 to 0.5, the
  one-factor-at-a-time invariant, the fixed parameters, the protocol window
  limits, the sender command line, and metrics-row parsing and rejection. The
  subprocess orchestration itself is exercised for real by
  `make -C C++ experiments`.
- `tests/test_validate_results.py` mutates one field of one run at a time and
  requires the validator to reject the file and name the run. A validator that
  cannot fail would be worthless, so the mutations are the real test.
- `tests/test_plot_results.py` parses every generated figure as XML and compares
  each plotted marker against the value it claims to represent.
- `tests/test_generate_report.py` requires the rendered report to substitute
  every placeholder, to reference its evidence by relative path only, to contain
  no machine-specific absolute path, and to render identically twice.

## 4. Results

### 4.1 Unimpaired baseline

{{BASELINE_TABLE}}

### 4.2 Figures

{{FIGURES}}

### 4.3 What the numbers show

{{OBSERVATIONS}}

## 5. Reproducing this report

```bash
make -C C++ all           # build build/sender and build/receiver
make -C C++ test          # unit, end-to-end, and tooling tests
make -C C++ experiments   # regenerate results/experiments.csv
make -C C++ results       # validate, plot, and re-render this report
```

The fixture itself is regenerated with:

```bash
uv run python C++/tools/generate_test_data.py \
    --output C++/test_data/input.bin --seed 20260831 --size 4096
```

Every run is pinned to one seed and the tools are deterministic, so re-running
the pipeline on the same binaries reproduces this report byte for byte.

## 6. Complete result set

Every run in [`../results/experiments.csv`](../results/experiments.csv):

{{RESULTS_TABLE}}
