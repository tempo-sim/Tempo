# tempo-sim

Rust client API for the [Tempo](https://temposimulation.com) simulation server.

This crate provides generated gRPC wrappers (built on [tonic](https://crates.io/crates/tonic)
and [prost](https://crates.io/crates/prost)) for interacting with Tempo simulation servers,
exposing both synchronous and asynchronous surfaces backed by [tokio](https://crates.io/crates/tokio).

## Installation

```toml
[dependencies]
tempo-sim = "0.4"
```

## Quick start

```rust,no_run
use tempo_sim::set_server;

fn main() {
    // Point the client at a running Tempo server.
    set_server("localhost", 10001);

    // Or, for a server on this machine listening on a Unix domain socket (Linux/macOS):
    // set_socket("sim-a.sock");

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

Each wrapper takes its request message, which its module re-exports, and has an `_async` twin. An
RPC whose request is empty takes nothing:

```rust,no_run
use tempo_sim::tempo_world;

fn main() -> Result<(), tempo_sim::TempoError> {
    let spawned = tempo_world::spawn_actor(tempo_world::SpawnActorRequest {
        actor_type: "StaticMeshActor".into(),
        ..Default::default()
    })?;
    let _actors = tempo_world::get_all_actors()?;
    tempo_world::destroy_actor(tempo_world::DestroyActorRequest { actor: spawned.name })?;
    Ok(())
}
```

Common helpers (`set_server`, `set_server_async`, `set_socket`, `set_socket_async`,
`tempo_context`, `TempoContext`, `TempoError`, `SyncStreamIterator`) are re-exported at the crate
root.

## The reflected API

The crate also carries the runtime of Tempo's
[reflected API](https://tempo-sim.readthedocs.io/en/latest/clients/reflected-api/), which reaches a
build's properties and functions through generated, compiler-checked types:

- `unreal`: Unreal's value types (`FVector`, `FRotator`, `FTransform`, ...), in centimeters,
  degrees and left-handed, and the handle every generated class is built on.
- `world`: finding, spawning and moving actors through those handles, and batching.
- `conversion`: `ue_to_si` and `si_to_ue`, between Unreal's values and the API's meters, radians
  and right-handed frame.

The classes themselves are generated per project, from its packaged build, and ship in that
project's own crate (`Packaged/API/Rust/<project>/`), which depends on this one.

## Documentation

Full API documentation is available at [docs.rs/tempo-sim](https://docs.rs/tempo-sim).

## License

Licensed under the [Apache License, Version 2.0](LICENSE).
