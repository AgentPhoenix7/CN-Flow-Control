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
