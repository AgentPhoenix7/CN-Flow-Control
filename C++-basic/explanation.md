# How `C++-basic` Works — A Complete, No-Assumptions Walkthrough

This document explains **every piece** of the `C++-basic/` project, starting from
"what is a network connection" all the way up to "why does Selective Repeat need a
map instead of an array." No prior networking or C++ knowledge is assumed. Wherever
useful, there's an ASCII diagram — think of them as little whiteboard sketches.

---

## Table of Contents

1. [The 30,000-foot view](#1-the-30000-foot-view)
2. [Background concepts you need first](#2-background-concepts-you-need-first)
3. [The problem this project solves](#3-the-problem-this-project-solves)
4. [The data frame: how one chunk of the file is packaged](#4-the-data-frame-how-one-chunk-of-the-file-is-packaged)
5. [CRC-16: how corruption gets detected](#5-crc-16-how-corruption-gets-detected)
6. [The simulated channel: manufacturing bad luck on purpose](#6-the-simulated-channel-manufacturing-bad-luck-on-purpose)
7. [Sockets: the actual network pipe](#7-sockets-the-actual-network-pipe)
8. [The "wire protocol" how messages don't get confused with each other](#8-the-wire-protocol-how-messages-dont-get-confused-with-each-other)
9. [The timer: guessing how long to wait](#9-the-timer-guessing-how-long-to-wait)
10. [Protocol 1 — Stop-and-Wait](#10-protocol-1--stop-and-wait)
11. [Protocol 2 — Go-Back-N](#11-protocol-2--go-back-n)
12. [Protocol 3 — Selective Repeat](#12-protocol-3--selective-repeat)
13. [The two executables: sender and receiver](#13-the-two-executables-sender-and-receiver)
14. [File-by-file map of the source code](#14-file-by-file-map-of-the-source-code)
15. [Running it yourself](#15-running-it-yourself)
16. [Glossary](#16-glossary)

---

## 1. The 30,000-foot view

Imagine you want to mail someone a **500-page book**, but your mailbox only accepts
envelopes that hold **46 pages** at a time, and the postal service is a little
unreliable — some envelopes get lost, some arrive torn open with pages missing or
smudged. You need a *system* for:

- chopping the book into 46-page envelopes,
- numbering them so they can be reassembled in order,
- detecting when an envelope arrived damaged,
- and re-sending anything that got lost or damaged —

all while making sure the final book is a **perfect, byte-for-byte copy** of the
original.

That's exactly what this project does, except the "book" is any file, the
"envelopes" are called **frames**, the "postal service" is a network connection we
deliberately sabotage a little (to prove the system is robust), and there are
**three different strategies** for how to send the envelopes and confirm they
arrived: **Stop-and-Wait**, **Go-Back-N**, and **Selective Repeat**.

```text
                     YOUR FILE (e.g. 537 bytes)
   ┌────────────────────────────────────────────────────┐
   │ "This is a small sample file used to exercise..."  │
   └────────────────────────────────────────────────────┘
                            │
                            │  chopped into 46-byte pieces
                            ▼
   ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐       ┌───────┐
   │ piece │ │ piece │ │ piece │ │ piece │  ...  │ piece │
   │  #0   │ │  #1   │ │  #2   │ │  #3   │       │  #11  │
   └───────┘ └───────┘ └───────┘ └───────┘       └───────┘
                            │
                            │  each piece wrapped in a "frame"
                            ▼
        ┌──────────┬─────────────┬──────┐
        │  header  │   payload   │ CRC  │   ← one frame
        │ 15 bytes │  46 bytes   │2bytes│
        └──────────┴─────────────┴──────┘
                            │
                            │  sent one at a time (or several at once)
                            │  over a network connection that we
                            │  deliberately corrupt/lose sometimes
                            ▼
   SENDER  ───────────────────────────────────▶  RECEIVER
     ▲                                                │
     └───────────────── ACK (acknowledgement) ────────┘
       "got frame #0, send me the next one"
```

There is a **sender program** (has the file, wants to transmit it) and a
**receiver program** (wants to end up with an exact copy of the file). They talk to
each other over one network connection. Three different "flow control" strategies
govern *how many envelopes can be in flight at once* and *how retransmission
works*. That's the whole project, at the highest level. Now let's build up the
vocabulary to understand every piece of it.

---

## 2. Background concepts you need first

### 2.1 What is a byte?

A **byte** is 8 bits (8 on/off switches), so it can hold a number from 0 to 255.
Files, network messages — everything in this project — are just long sequences of
bytes. When you see `0x8005` in this document, that's **hexadecimal** (base 16)
notation for a number; `0x8005` = 32773 in ordinary decimal. Hex is just a more
compact way to write byte patterns.

### 2.2 What is a network socket?

A **socket** is a programming handle for "a connection to another computer" (or, on
the same machine, another program). Think of it like a **telephone line**:

```text
   Computer A                                     Computer B
  ┌───────────┐                                  ┌───────────┐
  │  Program  │◀── socket (like a phone line) ──▶│  Program  │
  └───────────┘                                  └───────────┘
```

Once two programs have a socket connected to each other, either side can `send()`
bytes and the other side can `recv()` (receive) them. This project uses **TCP**
sockets specifically. TCP has two properties that matter a lot here:

- **It's a stream, not messages.** If you send 3 bytes, then 5 bytes, the other
  side might see them arrive as one chunk of 8 bytes, or as 1-byte-then-7-bytes, or
  any other split. TCP does **not** preserve your "I sent this as one unit"
  boundaries — you have to build that yourself (see [section 8](#8-the-wire-protocol-how-messages-dont-get-confused-with-each-other)).
- **It's reliable and in-order** *by itself* — TCP will never silently drop or
  reorder bytes for you. That might sound like it makes this whole project
  pointless ("isn't loss/corruption handling already TCP's job?") — but the
  assignment's entire point is to build that reliability *again*, ourselves, one
  layer up, the way real Data Link Layer hardware (Wi-Fi radios, Ethernet cards)
  has to, since TCP doesn't exist inside a network cable. So this project
  **pretends TCP is an unreliable wire** by randomly corrupting/dropping bytes
  itself, on purpose, before size TCP ever sees them lost.

### 2.3 What is a "port"?

A computer can run many programs at once, so a socket connection needs to say
*which* program on the other computer to talk to. That's what a **port number**
is — a number from 0-65535 that identifies "this particular conversation slot" on a
machine. In this project, the receiver "listens" on a port (default `5000`) and the
sender "connects" to that port.

### 2.4 Client vs. server

- The **receiver** acts as a **server**: it starts first, opens a port, and
  **waits** for someone to connect ("listening").
- The **sender** acts as a **client**: it actively **connects** to the receiver's
  address and port.

```text
   RECEIVER (server)                         SENDER (client)
  ┌──────────────────┐                      ┌──────────────────┐
  │ 1. open port 5000│                      │                  │
  │ 2. wait...       │                      │                  │
  │    wait...       │                      │ 3. connect to    │
  │    wait...       │◀─────────────────────│    127.0.0.1:5000│
  │ 4. accepted!     │                      │                  │
  └──────────────────┘                      └──────────────────┘
```

`127.0.0.1` is a special address that means "this same computer" (called
**loopback**) — the sender and receiver in this project usually run on the same
machine and talk to each other through the loopback address, no real network
hardware involved.

### 2.5 Network byte order ("big-endian")

When a multi-byte number (like a 2-byte length field) is put on the wire, everyone
has to agree on which byte comes first. This project always writes the
**most-significant byte first** (called *big-endian*, or "network byte order") —
e.g. the number 300 (`0x012C`) is written as the byte `0x01` then the byte `0x2C`.
This matters because C++ code must never just copy a number's raw memory layout
onto the wire (different computers store numbers differently in memory) — every
multi-byte field in this project is manually split into individual bytes.

### 2.6 What is XOR (`^`)?

XOR is a bit operation: `1 XOR 1 = 0`, `0 XOR 0 = 0`, `1 XOR 0 = 1`. It's used
twice in this project: to flip a single bit when *simulating* corruption
(`byte ^= (1 << bit_position)` toggles exactly one bit), and inside the CRC
calculation (see [section 5](#5-crc-16-how-corruption-gets-detected)).

### 2.7 What is a pseudo-random number generator (RNG) and a "seed"?

A computer can't generate *truly* random numbers on demand cheaply, so it uses a
**pseudo-random number generator**: an algorithm that produces a long sequence of
numbers that *look* random, but is entirely determined by a starting number called
a **seed**. Same seed → same sequence, every time, on every run. This project uses
seeds (`--seed`) so that "random" corruption/loss decisions are **reproducible** —
useful for writing down results in a report and re-running the exact same
experiment later.

---

## 3. The problem this project solves

The assignment (see `Assignment-2-CO2-FlowControl.pdf`) asks for a **Sender**
program and a **Receiver** program that transfer a file over a socket, where the
"physical wire" between them is deliberately unreliable (bytes can get corrupted,
frames can get lost/excessively delayed), and yet the receiver must still end up
with a **perfect copy** of the file. Three specific strategies for handling this
are required, differing in "how many frames can be in flight before you must
stop and wait for confirmation":

| Protocol         | Frames the sender may have "in flight" (unacknowledged) at once |
| ---------------- | --------------------------------------------------------------- |
| Stop-and-Wait    | 1                                                               |
| Go-Back-N        | `N` (a chosen window size, up to 255)                         |
| Selective Repeat | `N` (a chosen window size, up to 128)                         |

More frames in flight generally means faster transfers (less idle waiting), but
more complexity in tracking what's been confirmed and what needs re-sending.

---

## 4. The data frame: how one chunk of the file is packaged

Every 46-byte piece of the file gets wrapped into a **frame** before it's sent.
Here is the exact byte layout (this matches `include/frame.hpp`):

```text
  byte offset:   0         6         12    14   15                        61 62 63
                 │         │          │     │    │                         │  │  │
                 ▼         ▼          ▼     ▼    ▼                         ▼  ▼  ▼
                ┌──────────┬──────────┬─────┬────┬──────────────────────┬────────┐
                │  Source  │   Dest   │ Len │Seq │      Payload         │  CRC   │
                │   MAC    │   MAC    │     │    │   (46 bytes, zero-   │ (2     │
                │ (6 bytes)│ (6 bytes)│(2 B)│(1B)│    padded if short)  │ bytes) │
                └──────────┴──────────┴─────┴────┴──────────────────────┴────────┘
                 └────── HEADER (15 bytes) ──────┘                       └TRAILER┘

                Total frame size on the wire: 15 + 46 + 2 = 63 bytes
```

- **Source / Destination MAC (6 bytes each):** in real Ethernet hardware, a MAC
  address identifies a physical network card. Here they're just fixed placeholder
  values (`02:00:00:00:00:01` and `...:02`) included because the assignment's frame
  diagram requires them — this simulation only ever has one sender and one
  receiver, so they're not actually used to route anything.
- **Length (2 bytes):** the **true** number of meaningful payload bytes in *this*
  frame (0–46). Every frame's payload area is always 46 bytes wide on the wire, but
  the *last* frame of a file is often shorter than 46 real bytes — the leftover
  space is filled with zero bytes ("padding"), and Length is how the receiver knows
  to keep only the first `Length` bytes and discard the padding.
- **Sequence number / "Seq" (1 byte):** a counter, 0, 1, 2, 3, ... that identifies
  *which* piece of the file this frame carries. Because it's only 1 byte, it can
  only count 0–255 before it **wraps around** back to 0 — this is why the project
  cares about "sequence number wraparound" as a correctness case (a 500-frame file
  reuses sequence number 0 for its 257th frame, its 513th frame, etc.).
- **Payload (46 bytes):** the actual file bytes for this piece.
- **CRC (2 bytes):** a small "fingerprint" computed from everything before it (the
  15-byte header + the 46-byte payload), used to detect corruption. Explained fully
  in the next section.

```text
Splitting a 537-byte file into frames:

  file bytes: [0 ......................................................... 536]
               └──46───┘└──46───┘└──46───┘  ...  └──46───┘└─────25 bytes──────┘
                frame#0   frame#1   frame#2         frame#10      frame#11
                                                              (padded to 46 on
                                                               the wire, but
                                                               Length says 25)

  537 / 46 = 11.67, so 12 frames total (0 through 11), the last one partial.
```

---

## 5. CRC-16: how corruption gets detected

### 5.1 The idea in plain language

A **CRC (Cyclic Redundancy Check)** is a small number computed from a larger block
of bytes, such that if *even one bit* in that block changes, the CRC computed from
the new (corrupted) bytes will almost certainly be different from the original
CRC. It's like a fingerprint: you can't reconstruct the original data from it, but
you can tell if the data has changed.

```text
   Sender's side:                          Receiver's side:

   header + payload                        header + payload (maybe corrupted!)
         │                                        │
         ▼                                        ▼
   ┌─────────────┐                          ┌─────────────┐
   │ compute CRC │                          │ compute CRC │
   └─────────────┘                          └─────────────┘
         │                                        │
         ▼                                        ▼
     CRC = 0xAB12                      compare  CRC = 0xAB12 ?  ──▶ if equal:
         │                                                          "frame is
         ▼                                                           intact"
   attach CRC to the frame
   and send the whole thing            (mismatched CRC = discard the frame,
                                         don't write its payload, don't ACK it)
```

### 5.2 How the number is actually computed (`crc16_compute` in `src/crc16.cpp`)

The algorithm processes the data **one bit at a time**, maintaining a running
16-bit value (`crc`), starting at 0. For every bit of every byte:

1. Look at the current top bit of `crc`.
2. Shift `crc` left by one bit (this bit falls off the top and is discarded).
3. If that top bit *(before shifting)* was a 1, XOR `crc` with a fixed constant
   called the **polynomial**, `0x8005`.

```text
   for each byte in the data:
       crc = crc XOR (byte shifted into the top 8 bits)
       repeat 8 times (once per bit of this byte):
           if crc's top bit is 1:
               crc = (crc << 1) XOR 0x8005
           else:
               crc = (crc << 1)
```

Why does this detect corruption so reliably? Because a single flipped bit anywhere
in the input changes the running `crc` value in a way that (for a well-chosen
polynomial like `0x8005`) essentially never produces the same final CRC as the
original — the "collision" probability for a random single-bit or small-burst
error is extremely low. This project consistently uses the **same** CRC-16
(polynomial `0x8005`, starting value 0) on both the sender (to *compute* the
trailer) and the receiver (to *recompute* it and compare) — see `deserialize_and_verify`
in `src/frame.cpp`.

### 5.3 What happens on a CRC mismatch

If the receiver recomputes the CRC and it doesn't match the CRC that arrived in the
frame, the **entire frame is thrown away silently** — no payload is written to the
output file, and (per the assignment) **no ACK is sent for it**. From the sender's
point of view, a corrupted frame looks *exactly* like a *lost* frame: nothing comes
back, so eventually a timeout fires and the frame gets retransmitted. This is a
deliberate simplification, and it mirrors how real hardware behaves too — a
corrupted frame's receiver genuinely can't always tell "was that corruption, or
did nothing arrive at all?".

---

## 6. The simulated channel: manufacturing bad luck on purpose

Because this project runs on `127.0.0.1` (loopback), the network is actually
extremely reliable — bytes essentially never really get corrupted or lost in
transit. But the whole point of the assignment is to test what Stop-and-Wait /
Go-Back-N / Selective Repeat do **when things go wrong**. So the project
**fakes** bad luck on purpose, using the `Channel` class (`include/channel.hpp`,
`src/channel.cpp`):

```text
   Outgoing frame body (the 63 bytes about to be sent)
         │
         ▼
   ┌─────────────────────────────────────────┐
   │  roll a random number 0.0-1.0           │
   │  is it less than loss_prob?             │
   └─────────────────────────────────────────┘
         │                          │
        yes                        no
         │                          │
         ▼                          ▼
   DROP IT.                  ┌─────────────────────────────────┐
   (nothing is               │ roll ANOTHER random number      │
    sent at all —            │ is it less than error_prob?     │
    the receiver will        │                                 │
    simply never see         │  yes → flip one random bit      │
    this frame, and          │        in the frame             │
    the sender's timer       │  no  → send it untouched        │
    will eventually          └─────────────────────────────────┘
    time out)
```

Two independent probabilities control this, per direction:

- **`error_prob`** — chance that a delivered frame gets exactly **one bit** flipped
  somewhere inside it (using XOR, as described in [section 2.6](#26-what-is-xor-)).
  This simulates electrical/radio interference corrupting data in transit.
- **`loss_prob`** — chance the frame is **dropped entirely** (nothing is sent).
  This simulates a frame getting lost, or arriving so late that it might as well
  not have (the PDF calls this "random delay ... causes packet loss or timeout" —
  since a *very* late frame is indistinguishable from a lost one to a timer, this
  project treats them as the same thing rather than modeling delay separately).

**Crucially, there are two separate `Channel` objects** — one for the DATA
direction (owned by the sender, since the sender is the one physically putting
DATA frames "on the wire") and one for the ACK direction (owned by the receiver,
since the receiver is the one sending ACKs back). Each has its **own** random
number generator, seeded independently (`--seed` for the sender, `--seed` for the
receiver), so that "how unlucky the data path is" and "how unlucky the ACK path
is" never accidentally correlate with each other.

```text
        SENDER                                          RECEIVER
   ┌────────────────┐                              ┌────────────────┐
   │  DATA Channel  │── (maybe corrupted/dropped)─▶│                │
   │  (its own RNG) │       DATA frames            │                │
   └────────────────┘                              │                │
                                                   │  ACK Channel   │
   ┌───────────────┐◀──(maybe corrupted/dropped)───│  (its own RNG) │
   │               │         ACK messages          └────────────────┘
   └───────────────┘
```

---

## 7. Sockets: the actual network pipe

`include/socket.hpp` / `src/socket.cpp` wrap the low-level, C-style POSIX socket
API (functions like `socket()`, `bind()`, `listen()`, `accept()`, `connect()`,
`send()`, `recv()` — these come from the operating system, not from this project)
into a small C++ class called `TcpSocket`.

### 7.1 Why wrap it in a class at all?

Two reasons, both important C++ habits:

1. **RAII (Resource Acquisition Is Initialization).** A socket is an operating
   system resource (an integer called a *file descriptor*) that must be explicitly
   `close()`d when you're done, or it leaks. By putting the file descriptor inside
   a class and closing it in the class's **destructor** (the special function that
   runs automatically when the object goes out of scope), we get automatic cleanup
   for free — even if an error/exception happens partway through, C++ guarantees
   the destructor still runs.
2. **Move semantics, not copying.** You can't meaningfully "copy" a socket
   connection (there's only one real connection, not two) so this class **deletes**
   the copy constructor/assignment (the compiler refuses to compile any code that
   tries to copy a `TcpSocket`) and instead supports **moving** — transferring
   ownership from one `TcpSocket` object to another, leaving the original one
   "empty" (its file descriptor set to `-1`, meaning "not a real socket").

```text
   TcpSocket a = TcpSocket::connect_to("127.0.0.1", 5000);
   TcpSocket b = std::move(a);
        │
        └─▶ 'b' now owns the real connection.
            'a' is left empty (fd = -1) and will do nothing when destroyed.
```

### 7.2 The two "listening vs. connecting" factory functions

- `TcpSocket::listen_and_accept(port)` — used by the **receiver**. Opens a port,
  waits (blocks) until someone connects, and returns a socket for that one
  connection.
- `TcpSocket::connect_to(host, port)` — used by the **sender**. Actively connects
  out to a listening receiver.

### 7.3 Sending and receiving

- `send_exact(data)` — TCP's `send()` isn't guaranteed to send *all* the bytes you
  ask it to in one call (it might only manage to push out a partial chunk if the
  network is momentarily busy), so this function loops, calling the real `send()`
  repeatedly until every byte has actually gone out.
- `recv_exact_blocking(n)` — the receiving equivalent: keeps calling the real
  `recv()` until exactly `n` bytes have been collected, since a single `recv()`
  call might likewise return fewer bytes than you asked for.
- `set_recv_timeout_ms(ms)` / `recv_timed_byte(out)` — these two work together to
  implement **timeouts**. Setting a receive timeout (via the OS option
  `SO_RCVTIMEO`) means "if nothing arrives within `ms` milliseconds, give up and
  tell me instead of waiting forever." This is precisely how the sender detects
  "no ACK arrived in time → this must be a timeout, retransmit."

```text
   RecvStatus recv_timed_byte(out):

     ┌─────────────────────────────┐
     │ ask the OS for 1 byte,      │
     │ but give up after 'ms' ms   │
     └─────────────────────────────┘
              │
      ┌───────┼────────────┐
      ▼       ▼             ▼
    got a   nothing        connection
    byte    arrived in     was closed
      │      time             │
      ▼       ▼                ▼
     Ok    Timeout          Closed
```

---

## 8. The "wire protocol": how messages don't get confused with each other

Remember from [section 2.2](#22-what-is-a-network-socket) that TCP is just a
stream of bytes with **no built-in concept of "messages."** If the sender writes
"a DATA frame" and then later "a DONE signal," the receiver just sees one long
stream of bytes and has to figure out on its own where one message ends and the
next begins. `include/wire.hpp` / `src/wire.cpp` solve this with a simple scheme:
**every message starts with one "tag" byte that says what kind of message it is,
and each tag has a fixed, known body length.**

```text
  Tag byte     Meaning        Body that follows           Total message size
  ──────────   ────────────   ─────────────────────────   ───────────────────
  0x01 (DATA)  "here's a       63 bytes: the frame          64 bytes
                data frame"    (header+payload+CRC)
  0x02 (ACK)   "I got frame    2 bytes: the sequence         3 bytes
                number X"      number, repeated twice
  0x03 (DONE)  "no more        (nothing)                     1 byte
                frames coming"
  0x04         "OK, DONE       (nothing)                     1 byte
  (DONE_ACK)   received,
                you can exit"
```

Because every tag has a **fixed** body length, the receiver never has to guess
where a message ends: read 1 byte (the tag), look up how many more bytes that tag
implies, then read exactly that many more. This is exactly what
`try_recv_message()` does:

```text
RecvMessage try_recv_message(socket, timeout_ms):
    set the receive timeout to 'timeout_ms'
    status, tag = read one byte
    if status != Ok:  return {status}        ← timed out or connection closed
    (the tag has arrived, so the rest of this same message is already
     in flight right behind it — switch to blocking/no-timeout for the body)
    body = read exactly however-many-bytes-this-tag-implies
    return {Ok, tag, body}
```

This "tag + fixed body length" idea is deliberately **outside** the simulated data
frame — the PDF calls this "the channel is only a carrier": TCP's own byte stream
framing must **never** be corrupted by the simulated channel, or a single
simulated bit-flip could desynchronize the *entire rest of the connection*
(the receiver would no longer be able to tell where any future message starts).
Only the **inside** of a DATA message's 63-byte body, and the inside of an ACK
message's 2-byte body, are ever candidates for simulated corruption.

### 8.1 Why does the ACK body repeat the sequence number twice?

A DATA frame has a full CRC-16 to detect corruption. An ACK is much smaller (just
a sequence number), so instead of a full CRC, this project uses a cheap trick:
**write the sequence number twice**. If the ACK channel corrupts one bit
somewhere in those 3 bytes, there's a very high chance the two copies of the
sequence number will no longer match each other — and `parse_ack_body()` checks
exactly that:

```cpp
std::optional<std::uint8_t> parse_ack_body(const std::vector<std::uint8_t>& body) {
  if (body.size() != 2 || body[0] != body[1]) return std::nullopt;  // corrupted!
  return body[0];
}
```

If they don't match, the function returns "nothing" (`std::nullopt`, C++'s way of
saying "no valid value"), and the sender treats that exactly like it never
received an ACK at all — it just keeps waiting (and eventually times out).

---

## 9. The timer: guessing how long to wait

### 9.1 The core problem

The sender needs to decide: **"How long should I wait for an ACK before deciding
it's not coming and retransmitting?"** Too short, and it retransmits frames that
were actually just fine and about to be acknowledged (wasteful). Too long, and a
genuinely lost frame sits idle for ages before anything happens (slow).

The standard answer: **watch how long ACKs actually take to come back (the
"round-trip time," or RTT), and set the timeout a bit longer than that,** so it
adapts automatically to how fast or slow the connection currently is.

```text
   sender sends frame ──────────────────────────▶  (travels to receiver)
        │  start a stopwatch                              │
        │                                                 ▼
        │                                          receiver processes it,
        │                                          sends an ACK back
        │                                                  │
        ▼                                                  │
   ACK arrives ◀───────────────────────────────────────────┘
        │
        └─▶ stop the stopwatch: that elapsed time = one RTT sample
```

### 9.2 `RttTimer` (`include/timer.hpp` / `src/timer.cpp`)

This project uses a simple, well-known smoothing technique called an
**exponential moving average (EWMA)**: instead of trusting any single RTT
measurement (which can be noisy), it keeps a running "smoothed" estimate that
gradually shifts toward each new sample:

```text
   smoothed_rtt = 0.875 × smoothed_rtt  +  0.125 × new_sample

   (i.e., "keep 87.5% of what I already believed,
           blend in 12.5% of this new measurement")

   timeout = 2 × smoothed_rtt
             clamped between 20ms (never shorter — leaves room for normal
                                    scheduling jitter) and 5000ms (never longer)
```

Every time a *genuine* ACK for a *freshly-sent* (non-retransmitted) frame arrives,
`on_sample()` feeds the measured RTT into this formula, and the timeout for the
*next* wait gets recalculated. This is a deliberately simplified version of the
timeout logic real TCP uses (which additionally tracks *variance* in RTT and
uses "Karn's rule" to specifically avoid sampling from retransmitted frames, since
you can't be sure *which* transmission's ACK you're actually timing) — the
`README.md`'s "Known simplifications" section documents this trade-off honestly.

---

## 10. Protocol 1 — Stop-and-Wait

This is the simplest possible strategy: **never have more than one frame
un-acknowledged at a time.**

```text
  SENDER                                                    RECEIVER

  send frame #0  ─────────────────────────────────────────▶  (checks CRC — OK)
     │  (timer running)                                            │
     │                                                             ▼
     │                                             ◀───── ACK #0 ──
     │  ACK matches, timer stops
     ▼
  send frame #1  ─────────────────────────────────────────▶  (checks CRC — OK)
     │                                                              │
     │                                                              ▼
     │                                              ◀───── ACK #1 ──
     ▼
    ... and so on, one at a time ...
```

### 10.1 What happens on a timeout

If frame #1 gets lost (dropped by the simulated channel) or corrupted, no ACK
ever comes back, and eventually the timer expires:

```text
  SENDER                                                    RECEIVER

  send frame #1  ────────────────X  (dropped by the channel — never arrives)
     │
     │  ... timer expires (timeout!) ...
     ▼
  RE-send frame #1 (same frame, same sequence number) ─────▶  (checks CRC — OK)
     │                                                              │
     │                                                              ▼
     │                                              ◀───── ACK #1 ──
     ▼
```

### 10.2 What if the frame arrived fine, but the *ACK* got lost?

This is a subtler case: the receiver *did* get frame #1 and wrote it to the file,
but the ACK confirming that got lost on the way back. The sender doesn't know the
difference — all it knows is "no ACK arrived," so it retransmits. The receiver
must recognize this as a **duplicate** and:

- **not** write the payload to the file a second time (that would corrupt the
  output — duplicated bytes!),
- but **still** send an ACK back (because the sender is clearly still waiting for
  one).

```cpp
// from src/stop_and_wait.cpp (receiver side)
if (frame.seq == expected_seq) {
  // brand new frame: write it, ACK it, advance
  out.write(...);
  send_ack(frame.seq);
  expected_seq++;
} else if (have_delivered) {
  // this is a duplicate of the frame we already delivered — don't
  // write it again, but DO re-send the ACK, since the sender is
  // clearly still waiting for one.
  send_ack(expected_seq - 1);
}
```

### 10.3 The code, in plain terms

The sender's loop (`run_stop_and_wait_sender` in `src/stop_and_wait.cpp`), for each
frame of the file:

```text
for each frame of the file:
    acked = false
    while not acked:
        serialize the frame into 63 bytes
        maybe corrupt/drop it (the simulated Channel)
        start the timer
        send it (unless it was dropped)
        wait for a matching ACK, up to the current timeout
        if timed out:
            count a timeout + a retransmission, loop again (resend same frame)
        else if a valid ACK for this frame's sequence number arrived:
            feed the elapsed time into the RttTimer
            acked = true
        else:
            (corrupted ACK, or an ACK for the wrong sequence number — ignore it
             and keep waiting; the current wait will eventually time out)
```

---

## 11. Protocol 2 — Go-Back-N

Stop-and-Wait wastes a lot of time idling — the sender does *nothing* while
waiting for each individual ACK, even though the network could easily be carrying
several frames at once. **Go-Back-N** fixes this by allowing up to `N` frames to
be "in flight" (sent, but not yet acknowledged) simultaneously — this `N` is
called the **window size**.

```text
   Window size N = 4.  The sender can have frames #0, #1, #2, #3 all
   "in flight" at once, without waiting for any of their ACKs individually:

   frame #0 ──▶
   frame #1 ──▶
   frame #2 ──▶
   frame #3 ──▶      (all four sent, back to back, no waiting in between)

   base = 0 ─────────────────────────── next = 4
   [ #0 ][ #1 ][ #2 ][ #3 ]│ #4  #5  #6 ...
    ▲ outstanding window ▲  ▲ not sent yet
```

### 11.1 Cumulative ACKs

Go-Back-N's ACKs are **cumulative**: an ACK carrying sequence number `k` means "I
have correctly received everything up to *and including* frame `k`," not just
frame `k` alone. So once frame #2's ACK arrives, the sender knows #0, #1, *and* #2 are all confirmed — it can slide its window forward past all three at once

```text
   ACK for #2 arrives  ─────▶  base slides from 0 all the way to 3
                                (frames #0, #1, #2 are now considered done)

   base = 3 ─── next = 4
   [ #3 ]│ #4  #5  #6  #7 ...
          ▲ the window can now grow forward, sending #4
```

### 11.2 The receiver only accepts frames in strict order

Go-Back-N's **receiver window is fixed at 1** — meaning the receiver will *only*
accept the *next expected* frame, in strict order. If frame #2 arrives before
frame #1 (e.g. because #1 got dropped and is being retransmitted, but #2 sailed
through untouched), the receiver **throws frame #2 away** — it does not buffer it
for later — and simply re-sends the ACK for whatever it last correctly received
(so the sender knows to keep trying):

```text
   frame #0 arrives ✓ (expected #0) → deliver, ACK #0, now expecting #1
   frame #1 ────X (dropped by the channel — never arrives)
   frame #2 arrives, but receiver is expecting #1, not #2
        → DISCARD frame #2's payload (do not write it!)
        → re-send ACK #0 (the last thing actually confirmed)
   ... eventually frame #1 times out on the sender and gets retransmitted ...
   ... and because Go-Back-N retransmits the WHOLE outstanding window on
       timeout, frame #2 (and #3, if also outstanding) get resent too ...
```

### 11.3 Why "retransmit the *whole window*" on timeout?

Since the receiver refuses anything out of order, if frame #1 is lost, every frame
*after* #1 that the sender already sent is **useless** to the receiver until #1
arrives — they'll all just get discarded as "out of order," over and over, until

# 1 finally succeeds. So when a timeout happens, Go-Back-N doesn't just resend the

one missing frame — it resends **everything from the window's base onward**,
since all of it is presumed to now be "wasted" from the receiver's point of view:

```text
   window at time of timeout: [ #1 (never arrived) ][ #2 ][ #3 ]
                                                        (both discarded by
                                                         the receiver, since
                                                         it's still waiting
                                                         for #1)
        │
        │  timeout fires
        ▼
   RESEND #1, #2, AND #3 — the entire outstanding window, not just #1
```

This is the defining trade-off of Go-Back-N: it's simple, but a single lost frame
can force re-sending several frames that actually arrived fine the first time,
just because they arrived "out of order" and had to be thrown away.

### 11.4 The code, in plain terms

```text
base = 0, next = 0   (base = oldest un-acked frame, next = next frame to send)

while base < total number of frames:
    while the window has room (next - base < N) and there are more frames:
        send frame[next]; next += 1

    wait for one ACK, up to the current timeout

    if timed out:
        resend every frame from base up to (but not including) next
    else if a valid ACK for sequence number k arrived, and k is somewhere
             in the outstanding window [base, next):
        slide base forward to just past k
        (this naturally handles cumulative ACKs — sliding past k also
         slides past everything before k)
    else:
        (corrupted or stale ACK — ignore, keep going)
```

---

## 12. Protocol 3 — Selective Repeat

Go-Back-N's big weakness — throwing away perfectly good out-of-order frames — is
exactly what **Selective Repeat** fixes. Here, **both** the sender's and the
receiver's windows are size `N` (not just the sender's), acknowledgements are
**independent** (an ACK for frame #5 only confirms frame #5, nothing else), and
the receiver is allowed to **buffer** valid frames that arrive out of order,
instead of discarding them.

```text
   Window size N = 4.

   frame #0 ──▶  arrives fine, ACK #0 sent
   frame #1 ────X  (dropped — lost in transit)
   frame #2 ──▶  arrives fine! Even though #1 hasn't arrived yet,
                 the receiver BUFFERS #2 instead of discarding it,
                 and sends ACK #2.
   frame #3 ──▶  arrives fine, buffered, ACK #3 sent.

   Receiver's buffer:  next_deliver = 1
                        ┌─────┬─────┬─────┐
                        │  ?  │ #2  │ #3  │   (waiting on #1 specifically)
                        └─────┴─────┴─────┘

   ... eventually frame #1 times out and gets RE-sent (only #1 —
       not #2 or #3, since those already succeeded!) ...

   frame #1 (retransmit) ──▶ arrives, ACK #1 sent.
       NOW the receiver can deliver #1, #2, #3 all at once, in order,
       because the buffer is finally contiguous starting from #1:

   Receiver's buffer:  next_deliver = 1
                        ┌─────┬─────┬─────┐
                        │ #1  │ #2  │ #3  │  → flush all three to the
                        └─────┴─────┴─────┘    output file, in order,
                                                 next_deliver becomes 4
```

### 12.1 Only the missing frame gets retransmitted

Because ACKs are independent, the sender knows *exactly* which frames succeeded
and which didn't — it doesn't need to blindly resend the whole window like
Go-Back-N does. Each outstanding frame effectively has its **own** little timer
(implemented in this project by checking, every time the loop wakes up, "has this
specific frame been waiting longer than the current timeout?" for every
still-unacknowledged frame in the window):

```cpp
// from src/selective_repeat.cpp (sender side) — simplified
for each frame i still outstanding (not yet acked) in the window:
    if time since frame i was last sent >= current timeout:
        resend JUST frame i (mark it as a retransmission)
        (frames that already succeeded are never touched)
```

### 12.2 The receiver's buffer, and why it uses a `std::map`

The receiver needs a data structure that can hold "frame #2's payload" and "frame

# 3's payload" even though frame #1 (which comes *before* them) hasn't arrived yet

and can efficiently check "do I now have frame `next_deliver`?" after every new
arrival. This project uses a **`std::map<std::uint8_t, std::vector<std::uint8_t>>`**
— a map is a *key → value* lookup structure (here, sequence number → payload
bytes) that keeps its entries sorted by key and lets you check "does key `X`
exist?" quickly:

```cpp
// from src/selective_repeat.cpp (receiver side) — simplified
buffer[frame.seq] = payload_bytes;      // remember this frame's payload
send_ack(frame.seq);                     // independently ACK it

while (buffer contains next_deliver) {
    write buffer[next_deliver] to the output file
    remove it from the buffer
    next_deliver += 1
}
```

That `while` loop is the key idea: after adding one new frame to the buffer, keep
flushing out frames to the file for as long as the *next expected* sequence
number happens to already be sitting in the buffer — which naturally handles the
case where one retransmission (like #1 above) suddenly makes a whole backlog of
already-buffered frames deliverable at once.

### 12.3 Why the window is capped at 128 (not 255, like Go-Back-N)

Sequence numbers are only 1 byte, so they wrap around every 256 values (0, 1,
2, ..., 255, 0, 1, ...). Selective Repeat's receiver has to be able to tell "is
this sequence number a *new* frame within my current window, or is it an *old*,
already-delivered duplicate that happens to have wrapped back around to a
similar-looking number?" That distinction is only unambiguous if the window size
is **at most half** of the sequence number space (128 = 256 / 2) — with a bigger
window, a genuinely new frame and a very old duplicate could become
indistinguishable. Go-Back-N doesn't have this problem as severely (it only ever
accepts one specific next-expected sequence number at a time, not a whole range),
so it's allowed up to 255.

### 12.4 The code, in plain terms

```text
base = 0, next = 0
acked[]  = false for every frame

while base < total number of frames:
    while the window has room and there are more frames:
        send frame[next]; next += 1

    wait for one ACK, up to the current timeout
    if a valid ACK for sequence k arrived, and k is in the window and not
       already acked:
        mark acked[k] = true
        feed the RTT sample into the timer

    slide base forward past any already-acked frames at the front

    for every still-outstanding frame in the window:
        if it has personally been waiting longer than the current timeout:
            resend just that one frame
```

---

## 13. The two executables: sender and receiver

`src/sender_main.cpp` and `src/receiver_main.cpp` are the two actual programs you
run (`build/sender` and `build/receiver`). Their job is small and mechanical:

1. **Parse command-line flags** (`--protocol`, `--port`, `--window`, `--data-error`,
   etc.) into a small `Args` struct. This is done by hand with a simple loop over
   `argv` — no external argument-parsing library.
2. **Read the input file into memory** (sender only) — `read_file()` slurps the
   whole file into a `std::vector<std::uint8_t>` (a growable array of bytes).
3. **Open the socket** — the receiver calls `listen_and_accept()`, the sender calls
   `connect_to()`.
4. **Dispatch to the chosen protocol** — an `if`/`else if` chain picks which of
   the three protocol modules' sender/receiver function to call, based on
   `--protocol`.
5. **Report results.** The sender prints one line of statistics
   (`protocol=... elapsed_ms=... frames_sent=... retransmissions=... acks_received=... timeouts=...`)
   — this is the raw material for the report comparisons the assignment asks for
   (efficiency, RTT behavior, etc. — see `C++-basic/README.md`'s "What the sender
   prints" section for exactly how to turn these numbers into those comparisons).
6. **Error handling.** Both programs wrap their entire `main()` body in a
   `try { ... } catch (const std::exception& e) { ... }` block — if *anything*
   goes wrong anywhere (a bad flag, a network error, a protocol violation), it's
   reported on `stderr` with a clear prefix (`"sender: ..."` / `"receiver: ..."`)
   and the program exits with status code 1, instead of crashing uninformatively.

```text
   $ ./build/receiver --protocol go-back-n --port 5000 --window 8 --output out.bin
     │
     ├─▶ parse_args()
     ├─▶ TcpSocket::listen_and_accept(5000)     ← blocks here until a sender connects
     ├─▶ run_go_back_n_receiver(socket, "out.bin", ack_channel_config, seed)
     │        │
     │        └─▶ (this function runs until it sees a DONE message, then returns)
     └─▶ program exits normally
```

---

## 14. File-by-file map of the source code

```text
C++-basic/
├── include/                 ← the ".hpp" files: declarations (the "what exists"
│                                and "what does it look like from outside"),
│                                read by every .cpp file that uses that module
│
│   ├── crc16.hpp             one function: compute a CRC-16 over some bytes
│   ├── frame.hpp              the DataFrame struct + serialize/verify/split
│   ├── channel.hpp            the simulated corruption/loss engine
│   ├── socket.hpp              the RAII TCP socket wrapper
│   ├── wire.hpp                the tagged-message send/receive helpers
│   ├── timer.hpp                the adaptive RTT/timeout estimator
│   ├── run_stats.hpp             the small struct of counters printed at the end
│   ├── stop_and_wait.hpp          declares the Stop-and-Wait sender/receiver
│   ├── go_back_n.hpp              declares the Go-Back-N sender/receiver
│   └── selective_repeat.hpp       declares the Selective Repeat sender/receiver
│
├── src/                     ← the ".cpp" files: the actual implementations
│   ├── crc16.cpp
│   ├── frame.cpp
│   ├── channel.cpp
│   ├── socket.cpp
│   ├── wire.cpp
│   ├── timer.cpp
│   ├── stop_and_wait.cpp
│   ├── go_back_n.cpp
│   ├── selective_repeat.cpp
│   ├── sender_main.cpp         ← builds into the "sender" executable
│   └── receiver_main.cpp       ← builds into the "receiver" executable
│
├── test_data/
│   └── input.txt              a small sample file to transfer
│
├── Makefile                   build instructions (see below)
├── run_demo.sh                interactive menu-driven demo runner
├── README.md                  build/run reference documentation
└── .gitignore                 keeps build/ and output_files/ out of git
```

### 14.1 Why separate `.hpp` and `.cpp` files at all?

This is a standard C++ pattern:

- The `.hpp` ("header") file is like a **table of contents** — it lists what
  functions/classes exist and what their inputs/outputs are, but not *how* they
  work internally.
- The `.cpp` file has the actual **implementation** — the real logic.

Every `.cpp` file that wants to *use* another module `#include`s that module's
`.hpp` file (not its `.cpp` file). This lets, say, `sender_main.cpp` call
`run_go_back_n_sender()` without needing to see (or recompile, if unchanged) all
of `go_back_n.cpp`'s internals — it just needs to know the function exists and
what to pass it, which the `.hpp` file tells it.

### 14.2 The `basic_flow` namespace

Every piece of this project's code lives inside `namespace basic_flow { ... }`.
A **namespace** is just a named box that prevents naming collisions — if some
other library also happened to define a function called `serialize`, C++ wouldn't
get confused, because this project's version is really called
`basic_flow::serialize`. You'll see `basic_flow::` prefixes in `sender_main.cpp`
and `receiver_main.cpp` for exactly this reason (those two files intentionally
don't add a blanket `using namespace basic_flow;` to keep call sites unambiguous
about where each name comes from).

### 14.3 The Makefile

`Makefile` tells the `make` build tool how to turn the `.cpp` files into the two
final programs:

```text
   .cpp files  ──(compile: g++ -c ...)──▶  .o "object" files (one per .cpp)
                                                    │
                                                    │ (link: g++ ... -o sender/receiver)
                                                    ▼
                                       build/sender    build/receiver
```

Each `.cpp` file is compiled **separately** into a `.o` file first (this is why
changing just one `.cpp` file and re-running `make` only recompiles *that* file,
not everything — a real time-saver on larger projects), and then all the relevant
`.o` files are **linked** together into the final executable. Both `sender` and
`receiver` share almost all the same `.o` files (`crc16.o`, `frame.o`,
`channel.o`, `socket.o`, `wire.o`, `timer.o`, and all three protocol `.o` files) —
only `sender_main.o` vs. `receiver_main.o` differs between them.

The build flags (`-std=c++17 -Wall -Wextra -Wpedantic -Werror`) mean: "compile
using the C++17 language standard, turn on extra warning categories, and treat
every warning as a hard error" — this project compiles completely warning-free.

---

## 15. Running it yourself

### 15.1 The easy way: the interactive demo script

```bash
cd C++-basic
./run_demo.sh
```

This builds the project (if needed) and then walks you through a series of
numbered menus:

```text
Choose a protocol:
1) stop-and-wait
2) go-back-n
3) selective-repeat
>
```

...then (for Go-Back-N/Selective Repeat) a window size, then a channel
impairment level (Clean / Light / Moderate / Heavy / Custom), then an input file
— and finally runs one full transfer, printing the sender's statistics line and
confirming the output file is byte-identical to the input.

### 15.2 The manual way

```bash
make all                                    # build once

# terminal 1 (start the receiver first — it waits for a connection)
./build/receiver --protocol stop-and-wait --port 5000 --output out.bin

# terminal 2
./build/sender --protocol stop-and-wait --port 5000 --input test_data/input.txt
```

Add impairment:

```bash
./build/receiver --protocol go-back-n --port 5000 --window 8 --output out.bin \
  --ack-error 0.2 --ack-loss 0.2 --seed 20

./build/sender --protocol go-back-n --port 5000 --window 8 --input test_data/input.txt \
  --data-error 0.2 --data-loss 0.2 --seed 21
```

See `C++-basic/README.md` for the full flag reference.

---

## 16. Glossary

| Term                                                      | Meaning                                                                                                                                                                 |
| --------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Frame**                                           | One packaged unit of data sent over the (simulated) network — header + payload + CRC trailer.                                                                          |
| **Payload**                                         | The actual useful data inside a frame (a 46-byte piece of the file).                                                                                                    |
| **Header**                                          | The metadata in front of a frame's payload (addresses, length, sequence number).                                                                                        |
| **Trailer**                                         | Metadata*after* the payload — here, just the CRC.                                                                                                                    |
| **CRC (Cyclic Redundancy Check)**                   | A small "fingerprint" number computed from data, used to detect corruption.                                                                                             |
| **ACK (Acknowledgement)**                           | A small message sent back confirming "I received frame X successfully."                                                                                                 |
| **Sequence number**                                 | A counter (0-255, wrapping) identifying which piece of the file a frame carries.                                                                                        |
| **Sliding window**                                  | The set of frames currently "in flight" (sent but not yet confirmed) that the sender is allowed to have outstanding at once.                                            |
| **Cumulative ACK**                                  | An ACK meaning "everything up to and including this sequence number is confirmed," not just one frame.                                                                  |
| **Timeout**                                         | The sender's rule: "if no ACK arrives within this long, assume it's lost and retransmit."                                                                               |
| **RTT (Round-Trip Time)**                           | How long it takes for a frame to travel to the receiver and its ACK to travel back.                                                                                     |
| **Retransmission**                                  | Sending the same frame again, because its first attempt wasn't confirmed in time.                                                                                       |
| **Duplicate**                                       | A frame the receiver has already successfully processed once, arriving again (usually because its original ACK was lost). Must not be written to the output file twice. |
| **Socket**                                          | A programming handle representing one network connection between two programs.                                                                                          |
| **Port**                                            | A number identifying which program on a machine a connection is meant for.                                                                                              |
| **TCP**                                             | The reliable, ordered, byte-stream network protocol this project's socket connection uses as its underlying "carrier."                                                  |
| **Loopback (127.0.0.1)**                            | A special network address meaning "this same computer," used so sender and receiver can talk without any real network hardware.                                         |
| **Byte order / network byte order**                 | The agreed convention (most-significant byte first) for writing multi-byte numbers onto the wire.                                                                       |
| **Seed**                                            | A starting number for a pseudo-random number generator, making its "random" output exactly reproducible.                                                                |
| **RAII**                                            | A C++ pattern: tie a resource's cleanup (e.g. closing a socket) to an object's destructor, so it happens automatically.                                                 |
| **Move semantics**                                  | A C++ mechanism for transferring ownership of a resource (like a socket) between objects without copying it.                                                            |
| **Namespace**                                       | A named grouping that prevents naming collisions between different pieces of code.                                                                                      |
| **Header file (`.hpp`) / source file (`.cpp`)** | The declaration ("what exists") vs. the implementation ("how it works") of a piece of C++ code.                                                                         |
