# tempo-sim

Rust client API for the [Tempo](https://temposimulation.com) simulation server.

This crate provides generated gRPC wrappers (built on [tonic](https://crates.io/crates/tonic)
and [prost](https://crates.io/crates/prost)) for interacting with Tempo simulation servers,
exposing both synchronous and asynchronous surfaces backed by [tokio](https://crates.io/crates/tokio).

## Installation

```toml
[dependencies]
tempo-sim = "0.1"
```

## Quick start

```rust,no_run
use tempo_sim::set_server;

fn main() {
    // Point the client at a running Tempo server.
    set_server("localhost", 10001);

    // Use the generated API modules, e.g. tempo_core, tempo_sensors, tempo_world.
}
```

The API is organized into per-service modules:

- `tempo_core` / `tempo_core_editor`
- `tempo_agents` / `tempo_agents_editor`
- `tempo_geographic`
- `tempo_movement`
- `tempo_sensors`
- `tempo_world`

Common helpers (`set_server`, `set_server_async`, `tempo_context`, `TempoContext`,
`TempoError`, `SyncStreamIterator`) are re-exported at the crate root.

## Documentation

Full API documentation is available at [docs.rs/tempo-sim](https://docs.rs/tempo-sim).

## License

Licensed under the [Apache License, Version 2.0](LICENSE).
