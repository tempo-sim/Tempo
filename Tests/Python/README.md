# Tempo Python API tests

End-to-end tests for the generated `tempo_sim` client, run against a **packaged** Tempo build
(not the editor). These complement the C++ automation tests (`Scripts/Test.sh`), which cover
pure in-engine logic; this suite covers the client-facing API contract and behavior over gRPC.

## Groups (pytest markers)

Each marker is also a CI fan-out group — see `.github/workflows/test_packaged.yml`.

| Marker | Needs running sim? | Covers |
|---|---|---|
| `contract` | no (wheel only) | the generated package imports and exposes the expected per-module API surface |
| `core` | yes | TempoCore time control (level name, sim time, fixed-step determinism) |
| `world` | yes | TempoWorld spawn/query/move/destroy + meters/right-handed coordinate round trips |
| `movement` | yes | TempoMovement commandable-pawn listing + velocity command moves a pawn (skips if the map has no commandable pawns) |
| `sensors` | yes + **GPU** | TempoSensors data APIs (stream a camera frame). Needs `TEMPO_SIM_RENDER=1` and a GPU; **excluded from the default CI matrix.** |

> The `sensors` group renders, so it only runs when explicitly selected (`-m sensors`) with
> `TEMPO_SIM_RENDER=1` on a GPU-capable machine. To turn it on in CI, add `sensors` to the matrix
> in `tempo_build_and_package.yml` with a GPU `runner` and `render: true`.

## Running locally

Package the project first (`Scripts/Package.sh`), then:

```bash
Scripts/TestPythonAPI.sh            # all groups
Scripts/TestPythonAPI.sh core       # one group
```

`TestPythonAPI.sh` creates a venv, installs the `tempo-sim` wheel shipped in
`Packaged/API/Python/`, points `TEMPO_PACKAGED_BINARY` at the packaged launcher, and runs
pytest. JUnit reports land in `Saved/PythonTestReport/`.

Override the packaged location with `TEMPO_PACKAGED_DIR=/path/to/Packaged` and the port with
`TEMPO_SERVER_PORT`.

## How the sim is managed

`conftest.py` provides a session-scoped `sim_server` fixture that launches the packaged binary
headless (`-nullrhi -unattended ... -ServerPort=<port>`), polls `get_current_level_name()`
until the gRPC server answers, and shuts it down (`quit()`, then escalate) at the end. Tests
that need the sim depend on `sim_server`; `contract` tests don't, so a contract-only run never
launches it. The `fixed_step` fixture puts the sim in deterministic fixed-step mode for
time/motion assertions.

## In CI

`tempo_build_and_package.yml` builds and packages once (uploading the artifact), then fans out
to the reusable `test_packaged.yml` — parallel jobs per (Unreal version × group), across both the
Python and Rust clients (see `Tests/Rust/`). Each job downloads the same artifact, so the
expensive build runs once while the tests run in parallel.

`test_packaged.yml` is language-agnostic and reusable by downstream projects. To test your own
project's custom API surface, call it with your own `test_command` — e.g. reuse this runner but
point it at your tests:

```
TEMPO_PYTHON_TESTS_DIR=Tests/Python Plugins/Tempo/Scripts/TestPythonAPI.sh core
```

`TestPythonAPI.sh` installs **every** wheel under `Packaged/API/Python` (so both `tempo-sim` and your
project's generated client are importable) and honors `TEMPO_PYTHON_TESTS_DIR` /
`TEMPO_TEST_REPORT_DIR`.
