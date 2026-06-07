# Tempo Rust API tests

The Rust analog of `Tests/Python/` — end-to-end tests for the generated `tempo-sim` **Rust** client
(tonic/prost), run against a **packaged** Tempo build. They run as a parallel fan-out dimension
alongside the Python tests in CI.

## Groups

`cargo` test binaries (one file per `tests/*.rs`) play the role the pytest markers play on the
Python side. `Scripts/TestRustAPI.sh <group>` runs `cargo test --test <group>`.

| Group (`--test`) | Needs running sim? | Covers |
|---|---|---|
| `contract` | no (compile only) | the generated crate compiles and exposes the expected modules, functions, and proto types — a renamed/dropped symbol breaks the build |
| `integration` | yes | level name + spawn/query/destroy + coordinate round trip against a live sim |

## How the crate dependency resolves

The crate under test (`tempo-sim`) is the one shipped in a packaged build at
`Packaged/API/Rust/tempo-sim/` (requires the package to be built with `TEMPO_GEN_RUST_API=1`).
`Scripts/TestRustAPI.sh` copies it into `vendor/tempo-sim/` (git-ignored) so the path dependency in
`Cargo.toml` resolves, then runs `cargo test`.

## Running locally

Package with Rust generation on, then:

```bash
TEMPO_GEN_RUST_API=1 Scripts/Package.sh
Scripts/TestRustAPI.sh contract       # compile/surface check
Scripts/TestRustAPI.sh integration    # against a live sim (the script launches/tears it down)
```

`TestRustAPI.sh` launches the packaged sim headless for sim-requiring groups, waits for its gRPC port,
runs the tests single-threaded (they share the client's global connection context), and writes the
cargo log to `Saved/RustTestReport/` (override with `TEMPO_TEST_REPORT_DIR`).
