# Rust Client

Tempo can generate a Rust client crate alongside the Python package. Generation is **opt-in**,
since the Rust toolchain is not bundled with Unreal.

## Generating it

```bash
# Linux / macOS
export TEMPO_GEN_RUST_API=1

# Windows (cmd)
set TEMPO_GEN_RUST_API=1
```

With the variable set, the prebuild generates Rust wrappers, builds the crate, and produces a
packaged `.crate` at:

```text
<plugin_root>/TempoCore/Content/Rust/API/target/package/tempo-sim-<version>.crate
```

**System dependencies:** only the Rust toolchain (`cargo` + `rustc`, via
[rustup](https://rustup.rs/)). You do **not** need a system `protoc` — the proto code is
pre-compiled into the published crate, so consumers only need `cargo build`. At prebuild time
generation uses a vendored `protoc` via the `tempo-sim-codegen` helper crate; consumers never see
it.

## Consuming it

From another Rust project, depend on it by path or git ref:

```toml
[dependencies]
tempo-sim = { path = "<plugin_root>/TempoCore/Content/Rust/API" }
```

## The shape of the API

The wrapper API mirrors Python: each Tempo module becomes a Rust module with sync and async
functions for every RPC. The package name is `tempo-sim` but the import path is `tempo_sim` —
cargo translates hyphens to underscores.

```rust
use tempo_sim::{set_server_async, tempo_world};

#[tokio::main]
async fn main() -> Result<(), tempo_sim::TempoError> {
    set_server_async("localhost", 10001).await;
    let response = tempo_world::get_current_actor_state_async("MyActor".into()).await?;
    Ok(())
}
```

The asynchronous form carries an `_async` suffix; the synchronous one has the bare name and
blocks on a [tokio](https://crates.io/crates/tokio) runtime internally. The crate is built on
[tonic](https://crates.io/crates/tonic) and [prost](https://crates.io/crates/prost).

Arguments are **positional** and follow the request message's field order. Fluent builders
(`batch()`, `call()`) work the same as in Python — see
[TempoWorld](../plugins/tempo-world.md#batching-property-sets).

Re-exported at the crate root: `set_server`, `set_server_async`, `tempo_context`, `TempoContext`,
`TempoError`, `SyncStreamIterator`.

!!! note "Optional message fields"

    Generated wrappers take optional message-typed fields as `impl Into<Option<T>>`, so you can
    pass either the value or `None` without wrapping at the call site.

## Publishing your own crate

As with Python, your project crate is generated automatically once your project defines its own
Protobuf services — Tempo's services generate `tempo-sim`, and yours generate a project crate
(e.g. `tempo-sample`) that depends on it.

Set publish metadata in a `crate_info.json` next to each crate — for example
`Content/Rust/API/crate_info.json` for your project crate. Edit **that** file, not the generated
`Cargo.toml`, which is overwritten on every build. Supported fields include `version`, `license`,
`repository`, `homepage`, `readme`, `keywords`, and `categories`.

Then publish with cargo — `tempo-sim` first, since your project crate depends on it
(`--allow-dirty` because the generated sources are git-ignored):

```bash
(cd Plugins/Tempo/TempoCore/Content/Rust/API && cargo publish --allow-dirty)   # tempo-sim
(cd Content/Rust/API && cargo publish --allow-dirty)                           # your crate
```

## Using the pre-built crate instead of generating

If you only need a client for Tempo's **built-in** services, you don't need `TEMPO_GEN_RUST_API`
or an Unreal build at all:

```toml
[dependencies]
tempo-sim = "0.1"
```

Full generated API documentation for the published crate is on
[docs.rs/tempo-sim](https://docs.rs/tempo-sim).

!!! warning

    As with Python, the published `tempo-sim` crate contains only Tempo's built-in services, not
    custom RPCs you define. If your project has its own services, generate and use your project
    crate — it depends on `tempo-sim` and adds your modules.

    By default the generated project crate references `tempo-sim` by **local path**. Set
    `"tempo_sim_source": "registry"` in `crate_info.json` to depend on the published crates.io
    crate instead.

## Example clients

`ExampleClients/Rust/` contains `SensorPlayground`, `WorldPlayground` and `MovementPlayground` —
the Rust counterparts of the Python examples. `SensorPlayground` also demonstrates H.264 decode
with `ffmpeg-next`, which needs FFmpeg 8 development headers locally.

[:octicons-arrow-right-24: Example clients](examples.md)

## API reference

[:octicons-arrow-right-24: Every RPC, by module](../reference/api/index.md)
