# Prerequisites

## Compatibility

| | Supported |
|---|---|
| **Operating system** | Linux (Ubuntu 22.04 and 24.04), macOS 15.0 or newer (Apple silicon only), Windows 10 and 11 |
| **Unreal Engine** | 5.7 and 5.8 |

## Install the prerequisites

=== "Linux"

    - **Unreal**: download and install from
      <https://www.unrealengine.com/en-US/linux>
    - **curl**: `sudo apt update && sudo apt install curl`
    - **jq**: `sudo apt update && sudo apt install jq`

=== "macOS"

    - **Unreal**: install using the Epic Games Launcher
    - **jq**: `brew install jq`

=== "Windows"

    - **Unreal**: install using the Epic Games Launcher
    - **jq**: download
      `https://github.com/jqlang/jq/releases/latest/download/jq-win64.exe`, put it anywhere on
      your `PATH`, and make sure it is named `jq`

    Every Tempo script ships as both a `*.sh` and a `*.bat`. The `*.bat` versions run on Windows
    as-is, so nothing extra is required. If you would rather run the `*.sh` versions, do so from
    [Git Bash](https://gitforwindows.org/).

## Environment variables

`UNREAL_ENGINE_PATH`

:   **Required on Linux.** Points at your Unreal Engine installation directory — the folder
    containing `Engine`. On macOS and Windows Tempo finds Unreal automatically via your
    `.uproject` file, but setting this overrides that.

Several other environment variables opt into optional build behavior (Rust and C++ client
generation, skipping code generation). They are collected in the
[environment variable reference](../reference/environment.md).

## What you do *not* need

- **A custom Unreal build.** Tempo patches your existing engine installation in place via
  [engine mods](../guides/engine-mods.md).
- **ROS.** Tempo's primary interface is gRPC. ROS 2 support exists and is entirely optional —
  see [TempoROS](../plugins/tempo-ros.md).
- **To set up a Python environment by hand.** The build generates the `tempo_sim` package and a
  ready-to-use virtual environment at `<project_root>/TempoEnv`.

## Client-only machines

The prerequisites above are for **building and running the simulator**. A machine that only *talks
to* a Tempo server needs none of them — no Unreal, no build, not even a checkout. Install the
published client and point it at the sim:

```bash
pip install tempo-sim
```

```toml
[dependencies]
tempo-sim = "0.1"
```

The published packages cover Tempo's built-in services. If your project defines its own RPCs,
your users need the package your build generates instead.

[:octicons-arrow-right-24: Client APIs](../clients/index.md)
