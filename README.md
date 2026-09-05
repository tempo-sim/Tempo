# Tempo

https://github.com/user-attachments/assets/dfc7b28b-3b73-4603-a779-dd6e5b2acec9

Tempo is a collection of simulation-focused plugins for Unreal Engine. Tempo makes the power of Unreal accessible to simulation and robotics developers, including plugins for client APIs, sensor simulation, agent behaviors, and more.

Tempo is the foundation on which you can build a simulator for your unique application. Not sure where to start? Want some guidance from the authors? Find us on [![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/bKa2hnGYnw)

## 📖 Documentation

**Full documentation lives at [tempo-sim.readthedocs.io](https://tempo-sim.readthedocs.io/).**

| | |
|---|---|
| [Getting Started](https://tempo-sim.readthedocs.io/en/latest/getting-started/) | Prerequisites, installation, and a Hello World you can run in an hour |
| [Concepts](https://tempo-sim.readthedocs.io/en/latest/concepts/) | Deterministic time, units and coordinates, naming, architecture |
| [Plugins](https://tempo-sim.readthedocs.io/en/latest/plugins/) | TempoCore, TempoWorld, TempoSensors, TempoMovement, TempoAgents, TempoGeographic, TempoPCG, TempoROS, TempoROSBridge |
| [Client APIs](https://tempo-sim.readthedocs.io/en/latest/clients/) | Python, Rust and C++ clients, and the example clients |
| [gRPC API Reference](https://tempo-sim.readthedocs.io/en/latest/reference/api/) | Every service, RPC, message and field — generated from the `.proto` files |
| [Guides](https://tempo-sim.readthedocs.io/en/latest/guides/) | Adding your own services, packaging, CI, testing, troubleshooting |
| [Migration](https://tempo-sim.readthedocs.io/en/latest/migration/) | What changed between versions, and what you have to do |

## Why Tempo, and How It Compares
Tempo is for building your *own* robotics simulator on modern Unreal Engine and driving all of it from code - a foundation of composable plugins, not a turnkey product. Among great simulators (CARLA, AirSim/Colosseum, Gazebo, Webots, Isaac Sim, Genesis), here's an honest look at where it fits.

### What Tempo is good at

| Key feature | What it means for your simulator |
|---|---|
| **Modern Unreal + ecosystem**                | Runs on UE 5.7 / 5.8 - Lumen, Nanite, PCG, Niagara, MetaHuman, Chaos, and the huge library of real-time content the engine community ships, all for free.                                                                                                                                       |
| **gRPC API, no ROS required**                | The primary interface is [gRPC](https://grpc.io): language-agnostic, schema-first, with HTTP/2 streaming. Clients connect across machines and platforms (Rust on Linux → sim on a Mac) - a step up from the older msgpack-RPC interfaces some sims use, and not tied to an in-process language. |
| **Code-generated, reflection-based control** | One set of `.proto` files generates Python / Rust / C++ clients (sync + async). Spawn any actor and get/set *any* property over the wire - no engine code needed.                                                                                                                               |
| **High-fidelity sensors**                    | Cameras with multiple lens models (incl. wide-FOV fisheye), semantic + instance segmentation, 2D bounding boxes, and hardware H.264 streaming; lidar with per-beam calibration and material-derived reflectivity.                                                                               |
| **Deterministic time**                       | Pause / play / step and wall-clock vs. fixed-step time, all over the API - built for reproducible runs and data generation.                                                                                                                                                                    |
| **Native, optional ROS 2**                   | rclcpp runs in-process (no separate bridge process) and is entirely optional.                                                                                                                                                                                                                 |
| **Runs where you work**                      | Linux, Windows, and **macOS** (unusual among photorealistic engine-based sims). Develop locally on the hardware you already have.                                                                                                                                               |
| **Extensible by design**                     | Tempo is built entirely as Unreal plugins (plus a few engine patches). If you can build it in Unreal, you can build it alongside Tempo.                                                                                                                                                         |

### Where another tool may fit better

| If you need… | Consider |
|---|---|
| Massive-scale parallel RL (thousands of GPU envs, Gym-style) | Isaac Lab, Genesis |
| Contact-rich / legged-robot physics fidelity | Isaac Sim (PhysX), Genesis (differentiable) |
| To drop in existing robot descriptions (URDF/SDF/USD) | Gazebo, Isaac Sim |
| A turnkey AV stack with prebuilt maps & scenarios | CARLA |

**In short:** Tempo is the strongest fit for a **photorealistic, deeply customizable simulator on modern Unreal, controlled entirely from code, that runs on the hardware your team already has** - especially if you're comfortable in Unreal. It's a younger project with a smaller community and content library than the largest established sims, and it deliberately leaves the last mile - your scenarios, robots, and application - to you. That trade-off is the point.

## Compatibility
- Linux (Ubuntu 22.04 and 24.04), MacOS (15.0 or newer, Apple silicon only), Windows 10 and 11
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
Follow along the steps below with this video. Sound on!

https://github.com/user-attachments/assets/849cde96-a5a0-46e7-ab18-4fcbbc9fea8d

### TempoSample
The [TempoSample](https://github.com/tempo-sim/TempoSample) project is provided as a reference. If you are starting a new project, consider creating your repo using `TempoSample` as a [template](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-repository-from-a-template), and renaming the project with [Scripts/Rename.sh](https://github.com/tempo-sim/TempoSample/blob/main/Scripts/Rename.sh).

### Clone Tempo
To add Tempo to an existing project (if you are *not* starting with TempoSample), clone tempo to your project's `Plugins` directory:<br />
```
git submodule add https://github.com/tempo-sim/Tempo.git
git submodule update --init --recursive
```

### One-Time Setup
Run the `Setup.sh` (or `Setup.bat` on Windows) script (from the `Tempo` root, or from `Scripts/`) once. This script will:
- Modify your project's `*.Target.cs` files to use Tempo's custom toolchain, which is necessary for linking certain third party dependencies properly
- Install the Tempo Unreal Engine mods, making some changes to your installed Engine in-place
- Download third party dependencies
- Add git hooks to keep engine mods and third party dependencies up to date automatically as you check out different Tempo commits

> [!WARNING]
> `Setup.sh` accepts a `-skip-hooks` flag which suppresses installing the `post-checkout` and `post-merge` git hooks. This is intended only for developers actively modifying Tempo itself. If you are simply using Tempo as a dependency in your project, do not use this flag. See [Installation](https://tempo-sim.readthedocs.io/en/latest/getting-started/installation/#one-time-setup).

### Build and Run
Use the included `Scripts/Build.sh` and `Scripts/Run.sh` (or their `.bat` counterparts on Windows) to build your project and open it in Unreal Editor.

### Hello World
With your project open in Unreal Editor, activate the Tempo virtual environment (`source ./TempoEnv/bin/activate` on Linux & Mac, or `source ./TempoEnv/Scripts/activate` on Windows) and start a Python interpreter:

```python
import tempo_sim.tempo_core_editor as tce
import tempo_sim.tempo_world as tw

tce.play_in_editor()                            # Simulation should begin
tw.spawn_actor(actor_type="BP_SensorRig")       # A tripod with a TempoCamera on top appears
tw.set_float_property(actor="BP_SensorRig", component="TempoCamera",
                      property="FOVAngle", value=60.0)
```

> [!NOTE]
> You don't have to install any Python package or dependencies to use Tempo. The build step automatically generated the `tempo_sim` Python package and virtual environment.

The **[full Hello World walkthrough](https://tempo-sim.readthedocs.io/en/latest/getting-started/hello-world/)** adds streaming sensor images and stepping deterministic time.

### Package
Use the included `Scripts/Package.sh` (or `Package.bat` on Windows) to package your project into a standalone binary, which you can then run from the `Packaged` folder. See [Packaging](https://tempo-sim.readthedocs.io/en/latest/guides/packaging/).

### Client Packages (Python & Rust)
Building your project also generates client packages so you — or your users — can drive your Tempo server from outside Unreal: a Python package always, and a Rust crate when you opt in with `TEMPO_GEN_RUST_API`. Tempo's own services ship in the `tempo-sim` package/crate; your project's services, if you define any, go in a separate project package/crate that builds on top of it.

You can **publish** these to [PyPI](https://pypi.org/) / [crates.io](https://crates.io/) to share them, or — for a pure client project with no custom services — **consume the pre-built `tempo-sim`** straight from PyPI / crates.io without building at all. See [Client APIs](https://tempo-sim.readthedocs.io/en/latest/clients/) for the full workflow.

## Continuous Integration
If you would like to set up a GitHub actions pipeline to build, package, run, and/or release your Tempo project, check out the `build_and_package` reusable workflow in [.github/workflows](https://github.com/tempo-sim/Tempo/tree/main/.github/workflows). `TempoSample`'s [tempo_sample_build_and_package](https://github.com/tempo-sim/TempoSample/blob/main/.github/workflows/tempo_sample_build_and_package.yml) workflow is a good reference.

For larger projects, you can cut ~10–15 minutes per run by pulling a pre-modded Unreal image instead of applying engine mods in-workflow. See [Continuous Integration](https://tempo-sim.readthedocs.io/en/latest/guides/continuous-integration/).

## Issues
Something not working as expected? Are we missing a key feature you need? Feel free to send us an [issue](https://github.com/tempo-sim/Tempo/issues).

## Giving Back
Want to contribute to Tempo? We'll be happy to review your PR.

Improving the documentation counts — it lives in [`docs/`](docs) in this repository. See [Contributing to these docs](https://tempo-sim.readthedocs.io/en/latest/guides/documentation/) for how to build the site locally.

## Going Deeper
Each plugin has its own documentation page:

[TempoCore](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-core/) ·
[TempoWorld](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-world/) ·
[TempoSensors](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-sensors/) ·
[TempoMovement](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-movement/) ·
[TempoAgents](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-agents/) ·
[TempoGeographic](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-geographic/) ·
[TempoPCG](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-pcg/)

And, if you are using ROS:
[TempoROS](https://github.com/tempo-sim/TempoROS) ·
[TempoROSBridge](https://tempo-sim.readthedocs.io/en/latest/plugins/tempo-ros-bridge/)
