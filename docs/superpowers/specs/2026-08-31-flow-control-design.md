# C++17 Data Link Flow Control Design

## Scope

Complete the Assignment 2 C++17 implementation with the smallest auditable system that satisfies the PDF and `AGENTS.md`: Checksum-16 and CRC FCS support, deterministic channel impairment, Stop-and-Wait, Go-Back-N, Selective Repeat, framed file transfer over one bidirectional TCP connection, reproducible experiments, plots, and a generated report. Assignment 1 remains unchanged.

## Wire contracts

- A data frame has a manually serialized 15-byte header: source MAC 6 bytes, destination MAC 6 bytes, valid unpadded payload length 2 bytes in network order, and sequence number 1 byte.
- The transmitted payload is zero-padded as needed to keep the complete frame within 64–1518 bytes. The FCS covers the serialized header plus the complete padded payload.
- FCS schemes are Checksum-16, CRC-8 `0xD5`, CRC-10 `0x233`, CRC-16 `0x8005`, and CRC-32 `0x04C11DB7`. CRC processing is MSB-first with zero initial remainder, no reflection, and no final XOR. CRC-10 is left-aligned in its two-byte field.
- Every application record is externally prefixed by a two-byte network-order record length. The prefix is never included in FCS calculation or channel corruption.
- Internal record types are CONFIG, DATA, ACK, ROUND_END, ACK_END, COMPLETE, and COMPLETE_ACK. ACK bodies contain the sequence byte and its bitwise complement so ACK corruption is detectable.

## Component boundaries

- `config`: fixed protocol constants and compile-time frame-size checks.
- `checksum`, `crc`, `error_injection`: reusable error detection and deterministic bit mutation.
- `frame`: scheme selection, scheme-dependent padding, frame serialization, verification, and extraction.
- `record`: typed application-record encoding and validation, independent of sockets.
- `channel`: seeded error/delay decisions. Excessive delay suppresses delivery for the current timeout round. Frame-selection and bit-position RNG state are independent.
- `timer`: `steady_clock` measurements plus an EWMA timeout estimator. Valid, non-retransmitted RTT samples update `srtt` and `rttvar`; timeout is `srtt + 4*rttvar`, clamped to 10–2000 ms. Retransmitted samples are ignored under Karn's rule.
- `metrics`: counters and CSV/JSON-safe output values. Efficiency is unique payload bytes divided by total transmitted data-frame bytes.
- `stop_and_wait`, `go_back_n`, `selective_repeat`: deterministic sender and receiver state machines. They know frame indexes and sequence arithmetic but not sockets, FCS, or channel randomness.
- `socket`: move-only RAII descriptors, exact send/receive loops, and two-byte record framing.
- `sender` and `receiver`: command-line applications that compose the modules.

## Protocol execution

The applications use deterministic timeout rounds over one TCP connection, avoiding threads while retaining application-layer ARQ behavior:

1. Sender transmits the current protocol window. The data channel may suppress, corrupt, or deliver each DATA record.
2. Sender transmits ROUND_END.
3. Receiver verifies each delivered frame before passing it to its receiver state machine. Corrupted frames produce no ACK.
4. Receiver applies the ACK channel to each generated ACK, then transmits ACK_END.
5. Sender processes valid ACKs. Missing progress is a timeout; the protocol state chooses the next retransmissions.
6. When all frames are acknowledged, sender transmits COMPLETE and receiver answers COMPLETE_ACK.

Stop-and-Wait exposes one frame per round. Go-Back-N exposes the whole outstanding window, uses cumulative ACKs, and its receiver accepts only the next sequence. Selective Repeat exposes only unacknowledged frames, uses independent ACKs, buffers in-window out-of-order payloads, and delivers only contiguous bytes. Sequence arithmetic is modulo 256; GBN windows are 1–255 and SR windows are 1–128.

## Channel and timing model

Error and excessive-delay probabilities are independently configurable from 0.0 to 1.0 for DATA and ACK paths. A delay outcome represents arrival after the current timeout and is therefore suppressed for that round. A corruption outcome flips one deterministic MSB-indexed bit. Clean and impaired comparisons reuse the same input, payload size, windows, and seeds.

RTT uses `steady_clock` from original transmission to a valid ACK. Timeout rounds add the current estimator value to logical completion time, which keeps experiment comparisons meaningful without adding wall-clock sleeps.

## CLI and evidence

Receiver listens on a requested port and output path. Sender accepts protocol, FCS, input path, host, port, window, payload size, DATA/ACK error and delay probabilities, and seed. Receiver receives protocol configuration over the connection. Sender prints one machine-readable metrics row after completion.

Python tools generate deterministic fixtures, run the protocol/FCS/probability matrix, validate identities and denominators, create SVG plots, and render a Markdown report. End-to-end tests require byte-identical output for every protocol and FCS scheme and include deterministic loss/corruption cases.

## Failure handling and limits

Malformed records, frames, CLI values, probabilities, ports, window sizes, and scheme names fail explicitly. Rejected parse/serialization operations do not partially modify outputs. Socket EOF in the middle of a record is an error. Receiver output is written only from verified, in-order delivered payload. Experiments fail on nonzero subprocess status, malformed metrics, missing identities, or inconsistent counters.

