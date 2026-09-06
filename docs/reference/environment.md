# Environment Variables

## Required

`UNREAL_ENGINE_PATH`

:   **Required on Linux.** Your Unreal Engine installation directory — the folder containing
    `Engine`.

    On macOS and Windows, Tempo finds Unreal automatically via your `.uproject` file; setting this
    overrides that. Useful when you have several engine versions installed.

## Build behavior

`TEMPO_GEN_RUST_API`

:   Set to `1` to generate the Rust client crate alongside the Python package. Off by default,
    since the Rust toolchain is not bundled with Unreal — you need `cargo` and `rustc` from
    [rustup](https://rustup.rs/).

    [:octicons-arrow-right-24: Rust client](../clients/rust.md)

`TEMPO_GEN_CPP_API`

:   Set to `1` to generate the C++ client library — headers under
    `TempoCore/Content/Cpp/API/include/` and a static archive under `lib/<Platform>/`.

    [:octicons-arrow-right-24: C++ client](../clients/cpp.md)

## Testing

These are read by `Scripts/TestPythonAPI.sh` and `Scripts/TestRustAPI.sh`.

| Variable | Effect |
|---|---|
| `TEMPO_PACKAGED_DIR` | Where to find the packaged build. Defaults to `Packaged`. |
| `TEMPO_PACKAGED_BINARY` | The packaged launcher to run. Set by the scripts from `TEMPO_PACKAGED_DIR`. |
| `TEMPO_SERVER_PORT` | Port the test sim listens on, and the client connects to. |
| `TEMPO_PYTHON_TESTS_DIR` | Which pytest directory to run. Point it at your own project's tests. |
| `TEMPO_TEST_REPORT_DIR` | Where JUnit and cargo logs are written. |
| `TEMPO_SIM_RENDER` | Set to `1` to allow render-dependent (GPU) tests to run. |

[:octicons-arrow-right-24: Testing](../guides/testing.md)

## Command-line alternatives

Some settings can also be given on the command line to the Editor or a packaged binary, which
takes precedence over project settings until project settings are modified during an Editor
session:

```bash
UnrealEditor -ServerPort=10002
MyGame.sh -ServerPort=10002
```

Headless operation for CI:

```bash
MyGame.sh -nullrhi -unattended -ServerPort=10001
```
