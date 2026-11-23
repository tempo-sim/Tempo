# Tempo

<img width="5116" height="1978" alt="Screenshot 2026-11-23 at 8 43 16 AM" src="https://github.com/user-attachments/assets/66bb2061-aec2-4452-9b78-3e8d9a14f66e" />

Tempo is a collection of simulation-focused plugins for Unreal Engine. Tempo makes the power of Unreal accessible to simulation and robotics developers, including plugins for client APIs, sensor simulation, agent behaviors, and more. Tempo is the foundation on which you can build a simulator for your unique application - a foundation of composable plugins, not a self-contained simulator.

**Full documentation lives at [tempo-sim.readthedocs.io](https://tempo-sim.readthedocs.io/).**

Not sure where to start? Want some guidance from the authors? Find us on [![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/bKa2hnGYnw)

### Why Tempo

| | |
|---|---|
| **Modern Unreal + ecosystem**                | Runs on UE 5.7 / 5.8 - Lumen, Nanite, PCG, Niagara, MetaHuman, Chaos, and the huge library of real-time content the engine community ships. |
| **gRPC transport**                | Language-agnostic, schema-first, with HTTP/2 streaming. Clients connect across machines and platforms. |
| **[Reflected](https://tempo-sim.readthedocs.io/en/latest/clients/reflected-api/) API** | Access and control your full simulation state from the client with an auto-generated, strongly-typed client API for your project (in Python, Rust, and C++). |
| **High-fidelity sensors**                    | Cameras with multiple lens models (including fisheye), semantic + instance segmentation, 2D bounding boxes, and hardware H.264 streaming; Lidar with per-beam calibration and material-derived reflectivity.|
| **Deterministic time**                       | Pause / play / step and wall-clock vs. fixed-step time, all over the API - built for reproducible runs and data generation. |
| **[TempoROS](https://github.com/tempo-sim/TempoROS) (optional)**                  | rclcpp runs in-process (no separate bridge process) and is entirely optional. |
| **Broad Platform Support**                      | Linux, Windows, and **macOS**. |
| **Extensible by design**                     | Tempo is built entirely as Unreal plugins. You bring the content and the project. |

## Compatibility
- Linux, Mac, Windows
- Unreal Engine 5.7 and 5.8

## Prerequisites
- Linux:
  - Unreal: Download and install from https://www.unrealengine.com/en-US/linux
  - `curl`: `sudo apt update && sudo apt install curl`
  - `jq`: `sudo apt update && sudo apt install jq` 
- Mac:
  - Unreal: Install using Epic Games Launcher
  - `jq`: `brew install jq`
- Windows:
  - Unreal: Install using Epic Games Launcher
  - [Git Bash](https://gitforwindows.org/) (Run all Tempo `*.sh` scripts using Git Bash, or use the `*.bat` versions)
  - `jq`: Download `https://github.com/jqlang/jq/releases/latest/download/jq-win64.exe` and put it anywhere on your Path, like (`C:\Program Files\Git\cmd`) and make sure it's named `jq`

On Linux only, `UNREAL_ENGINE_PATH` must be set to your Unreal Engine installation directory (the folder containing `Engine`). On Mac and Windows, Tempo will attempt to automatically find Unreal via your uproject file, but you can still set this to override it.

See [Prerequisites](https://tempo-sim.readthedocs.io/en/latest/getting-started/prerequisites/) for the full list, and the [environment variable reference](https://tempo-sim.readthedocs.io/en/latest/reference/environment/) for the rest.

## Getting Started

### TempoSample
The [TempoSample](https://github.com/tempo-sim/TempoSample) project is provided as a reference. If you are starting a new project, consider creating your repo using `TempoSample` as a [template](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-repository-from-a-template), and renaming the project with [Scripts/Rename.sh](https://github.com/tempo-sim/TempoSample/blob/main/Scripts/Rename.sh).

### Clone Tempo
To add Tempo to an existing project (if you are *not* starting with TempoSample), clone tempo to your project's `Plugins` directory:<br />
```
git clone https://github.com/tempo-sim/Tempo.git
```

### One-Time Setup
Run the `Setup.sh` (or `Setup.bat` on Windows) script (from the `Tempo` root, or from `Scripts/`) once. This script will:
- Download third party dependencies
- Add git hooks to keep third party dependencies up to date automatically as you check out different Tempo commits

### Build and Run
Use the included `Scripts/Build.sh` and `Scripts/Run.sh` (or their `.bat` counterparts on Windows) to build your project and open it in Unreal Editor.

### Package
Use the included `Scripts/Package.sh` (or `Package.bat` on Windows) to package your project into a standalone binary, which you can then run from the `Packaged` folder. See [Packaging](https://tempo-sim.readthedocs.io/en/latest/guides/packaging/).

## Continuous Integration
If you would like to set up a GitHub actions pipeline to build, package, run, and/or release your Tempo project, check out the `build_and_package` reusable workflow in [.github/workflows](https://github.com/tempo-sim/Tempo/tree/main/.github/workflows). `TempoSample`'s [tempo_sample_build_and_package](https://github.com/tempo-sim/TempoSample/blob/main/.github/workflows/tempo_sample_build_and_package.yml) workflow is a good reference.

## Issues
Something not working as expected? Are we missing a key feature you need? Feel free to send us an [issue](https://github.com/tempo-sim/Tempo/issues).

## Giving Back
Want to contribute to Tempo? We'll be happy to review your PR.
