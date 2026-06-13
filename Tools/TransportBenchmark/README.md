# tempo-transport-benchmark

Microbenchmark for the Tempo gRPC transport layer: TCP loopback vs Unix domain socket.

Two binaries:

- `tempo-bench-server` — minimal echo gRPC service that listens on both TCP and a UDS at
  the same time, so a single run can hit both transports against an identical workload.
  No Tempo sim required — this is what makes the benchmark CI-runnable.

- `tempo-bench-client` — issues unary echoes (latency) and server-streaming echoes
  (throughput) over each transport and prints a comparison.

## Quick start

```bash
# in one terminal
cargo run --release --bin tempo-bench-server

# in another
cargo run --release --bin tempo-bench-client
```

Default run exercises both transports (`--transport both`) in both modes
(`--mode both`). The output ends with side-by-side TCP / UDS tables.

### Examples

```bash
# Latency only, 10k unary calls with a small payload
cargo run --release --bin tempo-bench-client -- --mode latency --latency-iters 10000

# Throughput at 1080p-color size (≈6MB)
cargo run --release --bin tempo-bench-client -- --mode throughput \
    --throughput-payload-bytes 6220800 --throughput-count 60

# Just UDS
cargo run --release --bin tempo-bench-client -- --transport uds
```

## Notes

- Use `--release` — debug builds skew the numbers heavily.
- The throughput stream allocates `payload_size` bytes per message server-side and one
  copy per send; the wall-clock cost is dominated by gRPC framing + transport, which is
  what we want to measure.
- Throughput numbers from short runs are dominated by stream-open overhead. Bump
  `--throughput-payload-bytes` and `--throughput-count` so the total payload is comfortably
  > 100 MB and the elapsed time is > 1 s before drawing conclusions about TCP vs UDS
  throughput. Latency at 16-byte payloads is the cleaner measurement of fixed per-call cost.
- UDS is only attempted on Unix targets (`cfg(unix)`). On Windows the client errors out
  for the UDS leg; Tempo's Python/C++ clients still work on Windows AF_UNIX since gRPC
  C++ ships native support there.
