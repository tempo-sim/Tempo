# Prerequisites

## Compatibility

| | Supported |
|---|---|
| **Operating system** | Linux (Ubuntu 22.04 and 24.04), macOS 15.0 or newer (Apple silicon only), Windows 10 and 11 |
| **Unreal Engine** | 5.7 and 5.8 |

!!! warning "Unreal 5.6 is no longer supported"

    Support for UE 5.6 was removed from Tempo's main branch. If you are pinned to 5.6, use a
    Tempo release from before that change.

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
    - **[Git Bash](https://gitforwindows.org/)**: run all Tempo `*.sh` scripts from Git Bash, or
      use the `*.bat` versions
    - **jq**: download
      `https://github.com/jqlang/jq/releases/latest/download/jq-win64.exe`, put it anywhere on
      your `PATH` (`C:\Program Files\Git\cmd` works), and make sure it is named `jq`

## Environment variables

`UNREAL_ENGINE_PATH`

:   **Required on Linux.** Points at your Unreal Engine installation directory — the folder
    containing `Engine`. On macOS and Windows Tempo finds Unreal automatically via your
    `.uproject` file, but setting this overrides that.

Several other environment variables opt into optional build behavior (Rust and C++ client
generation, skipping code generation). They are collected in the
[environment variable reference](../reference/environment.md).

## What you do *not* need

- **A Python installation or any pip packages.** The build generates the `tempo_sim` Python
  package and a virtual environment at `<project_root>/TempoEnv` for you.
- **ROS.** Tempo's primary interface is gRPC. ROS 2 support exists and is entirely optional —
  see [TempoROS](../plugins/tempo-ros.md).
- **A custom Unreal build.** Tempo patches your existing engine installation in place via
  [engine mods](../guides/engine-mods.md).
