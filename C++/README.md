# C++17 Data Link Layer Flow Control

This directory contains a C++17 simulation of reliable framed file transfer over a bidirectional TCP connection.

The project implements and compares:

- Stop-and-Wait ARQ;
- Go-Back-N ARQ; and
- Selective Repeat ARQ.

TCP is used only as the carrier. Loss, corruption, delay, acknowledgments, timeouts, and retransmissions are simulated at the application layer.


## Data-frame wire format

| Field | Size |
| --- | ---: |
| Source MAC address | 6 bytes |
| Destination MAC address | 6 bytes |
| Valid payload length | 2 bytes, network byte order |
| Sequence number | 1 byte |

The serialized header is 15 bytes. Each field is serialized individually; an object's in-memory representation is never copied directly to the wire. The length field records the actual unpadded payload length.


## Error-detection schemes

| Scheme | Polynomial | FCS size |
| --- | ---: | ---: |
| Checksum-16 | — | 2 bytes |
| CRC-8 | `0xD5` | 1 byte |
| CRC-10 | `0x233` | 2 bytes |
| CRC-16 | `0x8005` | 2 bytes |
| CRC-32 | `0x04C11DB7` | 4 bytes |

The FCS covers the serialized 15-byte header and the complete padded payload, excluding the FCS itself. Its serialized size depends on the selected scheme; it is not always four bytes.

## Frame sizing

A complete simulated data frame—including header, padded payload, and FCS—must be between 64 and 1518 bytes.

Short payloads are zero-padded only for transmission. The header's length field retains the number of valid file bytes so padding is never written to the received file.

The maximum valid payload is 1500 bytes for Checksum-16 and CRC-8/10/16, but 1499 bytes for CRC-32. A fixed 46-byte payload is safe for every scheme.

## TCP record framing

One bidirectional TCP connection carries explicit DATA, ACK, and TRANSFER_COMPLETE application records.

Each record is prefixed with an external two-byte record length in network byte order. This prefix is transport framing: it is not part of a simulated data frame, is not covered by the frame's FCS, and is never selected for simulated corruption. This prevents a corrupted simulated header from desynchronizing the TCP byte stream.


## ARQ protocol behavior

- **Stop-and-Wait:** The sender keeps one frame outstanding, advances only after its matching ACK, and retransmits the same frame after timeout.
- **Go-Back-N:** The sender window is `N`; the receiver window is one. ACKs are cumulative, out-of-order frames are discarded, and a timeout retransmits every outstanding frame starting at the window base.
- **Selective Repeat:** Both sender and receiver windows are `N`. ACKs are independent, valid out-of-order frames are buffered, contiguous frames are delivered in order, and only unacknowledged frames are retransmitted.

Sequence numbers are one byte and wrap modulo 256. Go-Back-N permits at most 255 outstanding frames, while Selective Repeat requires `N <= 128`.


## Building and testing

```bash
make -C C++ all    # strict C++17 build of build/sender and build/receiver
make -C C++ test   # unit, end-to-end, and Python tooling tests
make -C C++ clean  # remove generated build output
```


## Experiments, validation, plots, and report

The Python tooling under `tools/` runs in the repository's root `uv`
environment. There is deliberately no second `pyproject.toml` here, so every
tool is invoked through `uv run`.

```bash
make -C C++ experiments   # run the reproducible matrix -> results/experiments.csv
make -C C++ results       # validate, then plot and render the report
```

`experiments` runs all three protocols with no impairment and with probabilities
0.1 through 0.5 on each of the four impairment paths independently — DATA-path
bit corruption, DATA-path excessive delay, ACK-path bit corruption, and ACK-path
excessive delay — for 63 runs in total. This is a one-factor-at-a-time design:
each run sweeps one path and holds the other three at zero. Input file, FCS
scheme, payload size, window size, and seed are identical across every run so the
results are comparable. Each run launches `build/receiver` and `build/sender` as
subprocesses over loopback TCP, requires the reconstructed file to be
byte-identical to the input, and appends the sender's own metrics row to
`results/experiments.csv`. Any nonzero subprocess exit or unparseable metrics row
fails the whole matrix.

`results` runs three tools in sequence, and validation gates the other two:

1. `tools/validate_results.py` checks every counter identity, every denominator,
   and matrix completeness for 0.1–0.5, and exits nonzero naming the offending
   run if anything does not hold. It also flags runs whose mean RTT is not
   measurable because Karn's rule discarded every RTT sample.
2. `tools/plot_results.py` renders 16 SVG figures into `results/plots/`.
3. `tools/generate_report.py` renders `report/report.md` from
   `report/report_template.md`.

**Efficiency** is defined once, as `EFFICIENCY_DEFINITION` in
`tools/validate_results.py`, and imported everywhere else:

> `efficiency = unique_payload_bytes / transmitted_frame_bytes` — the unique
> application payload the receiver accepted and delivered, divided by every
> DATA-frame byte the sender put on the wire, including retransmissions, the
> 15-byte header, zero padding, and the FCS.

This matches `flow_control::Metrics::efficiency()` in `src/metrics.cpp`.

Individual tools can be run directly:

```bash
uv run python C++/tools/generate_test_data.py --output C++/test_data/input.bin \
    --seed 20260831 --size 4096      # or: make -C C++ fixture
uv run python C++/tools/run_experiments.py
uv run python C++/tools/validate_results.py
uv run python C++/tools/plot_results.py
uv run python C++/tools/generate_report.py
```

Generated evidence lives in `results/experiments.csv`, `results/plots/*.svg`, and
`report/report.md`; all three are committed so the report is reproducible from
the repository alone.
