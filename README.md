# CN-Flow-Control

A Computer Networks course assignment simulating Data Link Layer flow control
over TCP. Sender and receiver programs transfer a file through a single
bidirectional socket while three Automatic Repeat reQuest (ARQ) protocols are
implemented and compared:

- **Stop-and-Wait**
- **Go-Back-N**
- **Selective Repeat**

TCP is used only as the carrier. Framing, checksum/CRC error detection, bit
corruption, and excessive delay/loss are simulated at the application layer,
reusing and extending the framing, checksum/CRC, error-injection, and socket
modules from the course's prior assignment (kept unchanged in its own
repository).

## Status

Implementation, testing, and the experiment/report pipeline are complete and
verified on a clean rebuild. See [`PROGRESS.md`](PROGRESS.md) for the current
state, exact verification commands and results, and known limitations.

## Repository layout

| Path | Contents |
| --- | --- |
| [`C++/`](C++/) | The active C++17 implementation: headers, sources, tests, experiment/report tooling, and generated evidence. |
| [`AGENTS.md`](AGENTS.md) | Contributor guidelines, wire contract, and coding conventions (also `CLAUDE.md`, a symlink to the same file). |
| [`PROGRESS.md`](PROGRESS.md) | Authoritative current-state handoff: completed work, verification commands, and known limitations. |
| [`docs/`](docs/) | Design notes and implementation plans. |
| `Assignment-2-CO2-FlowControl.pdf`, `Assignment-2.m4a` | The assignment brief and the professor's recorded walkthrough. |
| `pyproject.toml`, `uv.lock` | The root `uv`-managed Python environment used by the experiment, validation, plotting, and report tooling under `C++/tools/`. |

## Quick start

```bash
make -C C++ all           # strict C++17 build of build/sender and build/receiver
make -C C++ test          # unit, end-to-end, and Python tooling tests
make -C C++ experiments   # run the reproducible 63-run experiment matrix
make -C C++ results       # validate the results, then plot and render the report
```

See [`C++/README.md`](C++/README.md) for the full wire format, protocol
behavior, build/test targets, and how the experiment and report pipeline
works.

## Documentation

- [`C++/README.md`](C++/README.md) — data-frame wire format, FCS schemes,
  ARQ protocol behavior, and the build/test/experiment commands.
- [`C++/report/report.md`](C++/report/report.md) — the generated report
  comparing all three protocols under no impairment and under bit corruption
  and excessive delay on both the DATA and ACK paths.
- [`PROGRESS.md`](PROGRESS.md) — conversation handoff and current verified
  project state.
- [`AGENTS.md`](AGENTS.md) — repository guidelines for anyone (human or
  agent) working in this codebase.

## License

MIT — see [`LICENSE`](LICENSE).
