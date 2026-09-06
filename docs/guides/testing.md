# Testing

Tempo has two layers of tests, covering different things.

| Layer | Runs against | Covers |
|---|---|---|
| **C++ automation tests** (`Scripts/Test.sh`) | the Editor | pure in-engine logic |
| **Client API tests** (`Scripts/TestPythonAPI.sh`, `Scripts/TestRustAPI.sh`) | a **packaged** build | the client-facing API contract and behavior over gRPC |

## C++ automation tests

```bash
Plugins/Tempo/Scripts/Test.sh
```

These are Unreal automation tests over Tempo's in-engine logic — no client, no gRPC.

## Python client API tests

End-to-end tests for the generated `tempo_sim` client, run against a packaged Tempo build.

```bash
Plugins/Tempo/Scripts/Package.sh
Plugins/Tempo/Scripts/TestPythonAPI.sh            # all groups
Plugins/Tempo/Scripts/TestPythonAPI.sh core       # one group
```

`TestPythonAPI.sh` creates a venv, installs **every** wheel under `Packaged/API/Python/` (so both
`tempo-sim` and your project's generated client are importable), points `TEMPO_PACKAGED_BINARY` at
the packaged launcher, and runs pytest. JUnit reports land in `Saved/PythonTestReport/`.

### Groups

Each pytest marker is also a CI fan-out group.

| Marker | Needs a running sim? | Covers |
|---|---|---|
| `contract` | no (wheel only) | the generated package imports and exposes the expected per-module API surface |
| `core` | yes | TempoCore time control — level name, sim time, fixed-step determinism |
| `world` | yes | TempoWorld spawn / query / move / destroy, plus meters and right-handed round trips |
| `movement` | yes | TempoMovement commandable-pawn listing, and a velocity command moving a pawn (skips if the map has no commandable pawns) |
| `sensors` | yes **+ GPU** | TempoSensors data APIs (stream a camera frame) |

!!! note "`sensors` is excluded from the default CI matrix"

    It renders, so it only runs when explicitly selected (`-m sensors`) with `TEMPO_SIM_RENDER=1`
    on a GPU-capable machine. To turn it on in CI, add `sensors` to the matrix in
    `tempo_build_and_package.yml` with a GPU `runner` and `render: true`.

### How the sim is managed

`conftest.py` provides a session-scoped `sim_server` fixture that launches the packaged binary
headless (`-nullrhi -unattended … -ServerPort=<port>`), polls `get_current_level_name()` until the
gRPC server answers, and shuts it down (`quit()`, then escalate) at the end.

Tests that need the sim depend on `sim_server`; `contract` tests don't, so a contract-only run
never launches it. The `fixed_step` fixture puts the sim in deterministic
[fixed-step mode](../concepts/time.md#fixed-step-mode) for time and motion assertions.

The sim's full log is written to `TEMPO_TEST_REPORT_DIR/sim.log` and uploaded as a CI artifact, so
startup failures can be diagnosed.

### Overrides

| Variable | Effect |
|---|---|
| `TEMPO_PACKAGED_DIR` | Where to find the packaged build (default `Packaged`). |
| `TEMPO_SERVER_PORT` | Port to launch and connect on. |
| `TEMPO_PYTHON_TESTS_DIR` | Which test directory to run — point it at your own tests. |
| `TEMPO_TEST_REPORT_DIR` | Where reports are written. |
| `TEMPO_SIM_RENDER` | Set to `1` to allow render-dependent tests. |

## Rust client API tests

The Rust analog, for the generated `tempo-sim` Rust crate (tonic/prost). Cargo test binaries — one
file per `tests/*.rs` — play the role the pytest markers play on the Python side.

```bash
TEMPO_GEN_RUST_API=1 Plugins/Tempo/Scripts/Package.sh
Plugins/Tempo/Scripts/TestRustAPI.sh contract       # compile / surface check
Plugins/Tempo/Scripts/TestRustAPI.sh integration    # against a live sim
```

| Group (`--test`) | Needs a running sim? | Covers |
|---|---|---|
| `contract` | no (compile only) | the generated crate compiles and exposes the expected modules, functions and proto types — a renamed or dropped symbol breaks the build |
| `integration` | yes | level name, spawn / query / destroy, and a coordinate round trip against a live sim |

The crate under test is the one shipped in a packaged build at `Packaged/API/Rust/tempo-sim/`,
which requires packaging with `TEMPO_GEN_RUST_API=1`. `TestRustAPI.sh` copies it into
`Tests/Rust/vendor/tempo-sim/` (git-ignored) so the path dependency in `Cargo.toml` resolves, then
runs `cargo test`.

It launches the packaged sim headless for sim-requiring groups, waits for its gRPC port, runs the
tests **single-threaded** (they share the client's global connection context), and writes the
cargo log to `Saved/RustTestReport/`.

## Testing your own project's API

`test_packaged.yml` is language-agnostic and reusable by downstream projects. To test your own
project's custom API surface, call it with your own `test_command` — for example, reusing this
runner but pointing it at your tests:

```bash
TEMPO_PYTHON_TESTS_DIR=Tests/Python Plugins/Tempo/Scripts/TestPythonAPI.sh core
```

Because `TestPythonAPI.sh` installs every wheel under `Packaged/API/Python`, both `tempo-sim` and
your project's client are importable in your tests.
