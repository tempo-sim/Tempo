# Tempo

https://github.com/user-attachments/assets/dfc7b28b-3b73-4603-a779-dd6e5b2acec9

Tempo is a collection of simulation-focused plugins for Unreal Engine. Tempo makes the power of Unreal accessible to simulation and robotics developers, including plugins for client APIs, sensor simulation, agent behaviors, and more.

Tempo is the foundation on which you can build a simulator for your unique application. Not sure where to start? Want some guidance from the authors? Find us on [![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/bKa2hnGYnw)

## Why Tempo, and How It Compares
Tempo is for building your *own* robotics simulator on modern Unreal Engine and driving all of it from code - a foundation of composable plugins, not a turnkey product. Among great simulators (CARLA, AirSim/Colosseum, Gazebo, Webots, Isaac Sim, Genesis), here's an honest look at where it fits.

### What Tempo is good at

| Key feature | What it means for your simulator |
|---|---|
| **Modern Unreal + ecosystem**                | Runs on UE 5.6 / 5.7 - Lumen, Nanite, PCG, Niagara, MetaHuman, Chaos, and the huge library of real-time content the engine community ships, all for free.                                                                                                                                       |
| **gRPC API, no ROS required**                | The primary interface is [gRPC](https://grpc.io): language-agnostic, schema-first, with HTTP/2 streaming. Clients connect across machines and platforms (Rust on Linux → sim on a Mac) - a step up from the older msgpack-RPC interfaces some sims use, and not tied to an in-process language. |
| **Code-generated, reflection-based control** | One set of `.proto` files generates Python / Rust / C++ clients (sync + async). Spawn any actor and get/set *any* property over the wire - no engine code needed.                                                                                                                               |
| **High-fidelity sensors**                    | Cameras with multiple lens models (incl. wide-FOV fisheye), semantic + instance segmentation, 2D bounding boxes, and hardware H.264 streaming; lidar with per-beam calibration and material-derived reflectivity.                                                                               |
| **Deterministic time**                       | Pause / play / step and wall-clock vs. fixed-step time, all over the API - built for reproducible runs and data generation.                                                                                                                                                                     |
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

</details>

## Compatibility
- Linux (Ubuntu 22.04 and 24.04), MacOS (15.0 or newer, Apple silicon only), Windows 10 and 11
- Unreal Engine 5.6 and 5.7

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

## Environment Variables
- `UNREAL_ENGINE_PATH`: On Linux only must be set to your Unreal Engine installation directory (the folder containing `Engine`). On Mac and Windows, Tempo will attempt to automatically find Unreal via your uproject file, but you can still set this to override it.

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
> `Setup.sh` accepts a `-skip-hooks` flag which suppresses installing the `post-checkout` and `post-merge` git hooks. This is intended only for developers actively modifying Tempo itself — for example, when iterating on Tempo source while not touching `EngineMods/` or third party dependencies, the hooks can add noticeable overhead to every `git checkout`/`git merge`. Without the hooks, engine mods and third party deps will *not* re-sync automatically when you change Tempo commits, and you must run `Scripts/InstallEngineMods.sh` and `Scripts/SyncDeps.sh` manually to keep them in sync. If you are simply using Tempo as a dependency in your project, do not use this flag.

### Build and Run
Use the included `Scripts/Build.sh` and `Scripts/Run.sh` (or their `.bat` counterparts on Windows) to build your project and open it in Unreal Editor.

### Hello World
> [!NOTE]
> You don't have to install any Python package or dependences to use Tempo. The build step automatically generated the `tempo_sim` Python package and virtual environment, which will be used below.
1. With your Tempo project open in Unreal Editor, from the root of your project, activate the Tempo virtual environment (`source ./TempoEnv/bin/activate` on Linux & Mac, or `source ./TempoEnv/Scripts/activate` on Windows)
2. Start the Python interpreter (`python` on Linux & Mac, or `winpty python` on Windows) and use the Tempo API to start the simulation:
```
import tempo_sim.tempo_core_editor as tce
tce.play_in_editor() # Simulation should begin
```
3. Use TempoWorld to add an Actor to your scene:
```
import tempo_sim.tempo_world as tw
tw.spawn_actor(type="BP_SensorRig") # An Actor with a tripod mesh should appear. It has a TempoCamera on top (although it may not be visible).
```
4. From another terminal, run the included [SensorPlayground](https://github.com/tempo-sim/Tempo/blob/main/ExampleClients/Python/SensorPlayground.py) example client: `python ./Plugins/Tempo/ExampleClients/Python/SensorPlayground.py`. Use it to start streaming color images from the `TempoCamera`
5. While streaming images, from the Python interpreter again, use TempoWorld to change one of the Camera's properties:
```
tw.set_float_property(actor="BP_SensorRig", component="TempoCamera", property="FOVAngle", value=60.0) # The field of view of your streaming images should decrease
```
6. Lastly, use TempoCore to pause, resume, and step the simulation:
```
import tempo_sim.tempo_core as tc
import tempo_sim.TempoCore.Time_pb2 as Time
tc.pause() # Time should pause
tc.play() # Time should resume
tc.set_time_mode(Time.TM_FIXED_STEP) # Time mode should switch to Fixed Step, simulation should run faster than real-time
tc.step() # Time should advance to the nearest whole number of fixed time steps (0.1 seconds by default)
tc.step() # Time should advance one step. You should get one new camera image every step.
```
Congratulations, you are officially up and running! Continue experimenting with Tempo by creating new scenes, streaming sensor data, and varying the many properties of your simulation at runtime.

### Package
Use the included `Scripts/Package.sh` (or `Package.bat` on Windows) to package your project into a standalone binary, which you can then run from the `Packaged` folder.

### Client Packages (Python & Rust)
Building your project also generates client packages so you — or your users — can drive your Tempo server from outside Unreal: a Python package always, and a Rust crate when you opt in with `TEMPO_GEN_RUST_API`. Tempo's own services ship in the `tempo-sim` package/crate; your project's services, if you define any, go in a separate project package/crate that builds on top of it.

You can **publish** these to [PyPI](https://pypi.org/) / [crates.io](https://crates.io/) to share them, or — for a pure client project with no custom services — **consume the pre-built `tempo-sim`** straight from PyPI / crates.io without building at all. If your project defines its own custom RPCs, use the generated packages rather than the stock `tempo-sim`, which only knows Tempo's built-in services. See [TempoCore's README](/TempoCore/README.md#using-the-python-api) for the full workflow.

## Continuous Integration
If you would like to set up a GitHub actions pipeline to build, package, run, and/or release your Tempo project, check out the `build_and_package` reusable workflow in [.github/workflows](https://github.com/tempo-sim/Tempo/tree/main/.github/workflows). `TempoSample`'s [tempo_sample_build_and_package](https://github.com/tempo-sim/TempoSample/blob/main/.github/workflows/tempo_sample_build_and_package.yml) workflow is a good reference.

### Speeding Up Builds With a Pre-Modded Engine Image (optional)
Tempo modifies the Unreal engine in-place via patches in `EngineMods/`. By default `build_and_package.yml` applies these mods inside each CI run, which adds ~10–15 minutes and consumes the GitHub Actions cache budget (10 GB on the free tier, shared with the per-commit build cache). For larger projects you can opt into a pre-modded Unreal image hosted on your own private GHCR, which the build workflow pulls instead of re-applying mods on every run.

Add one new workflow file to your repo that calls Tempo's reusable `publish_engine_mods.yml`:

```yaml
# .github/workflows/publish_engine_mods.yml
name: Publish Pre-Modded Engine Image
on:
  push:
    branches: [main]
    paths:
      - 'Plugins/Tempo/EngineMods/**'
      - 'Plugins/Tempo/Dockerfile'
      - 'Plugins/Tempo/Scripts/InstallEngineMods.sh'
  workflow_dispatch:
jobs:
  publish:
    permissions:
      contents: read
      packages: write
    strategy:
      matrix:
        unreal_version: ["5.6", "5.7"]
      fail-fast: false
    uses: tempo-sim/Tempo/.github/workflows/publish_engine_mods.yml@main
    with:
      unreal_version: ${{ matrix.unreal_version }}
      image_name: ghcr.io/${{ github.repository_owner }}/tempo-unreal-modded
      tempo_root: Plugins/Tempo  # adjust to your Tempo submodule path
    secrets: inherit
  prune:
    needs: publish
    permissions:
      packages: write
    uses: tempo-sim/Tempo/.github/workflows/prune_engine_mods.yml@main
    with:
      package_name: tempo-unreal-modded
      package_owner: ${{ github.repository_owner }}
    secrets: inherit
```

Then pass `engine_mods_image: ghcr.io/<your-org>/tempo-unreal-modded` to your existing `build_and_package.yml` caller, and grant it `packages: read`. The build workflow will compute the EngineMods hash, attempt `docker pull` of the matching tag, and skip the in-workflow engine-mods init/install when the pull succeeds. If the pull fails for any reason (image not yet published for the current hash, permissions misconfigured, etc.) the workflow falls back to the in-workflow path, so there's no breakage — just no speedup.

Setup notes:
- You'll need `EPIC_DOCKER_USERNAME` and `EPIC_DOCKER_TOKEN` secrets configured to pull Epic's base Unreal image. Same requirement as the plain `build_and_package.yml`.
- Run your new `publish_engine_mods` workflow manually once via `workflow_dispatch` before merging the `engine_mods_image` change to your build workflow, so the first image is available when the build runs.
- The published image contains UE-derived content, which the Unreal Engine EULA does not permit redistributing publicly. Your org's GHCR package-creation policy must be set to "Private" (under `Settings → Packages` in the org admin), and the package will inherit private visibility on first push. If your org's package creation policy allows public, flip the package to private via the package's settings page after the first push.
- The `prune` job keeps only the most recent tag per Unreal version, which is sufficient since EngineMods rarely change. If you'd prefer a longer history, pass `keep: 3` (or higher) to `prune_engine_mods.yml`.

## Issues
Something not working as expected? Are we missing a key feature you need? Feel free to send us an [issue](https://github.com/tempo-sim/Tempo/issues).

## Giving Back
Want to contribute to Tempo? We'll be happy to review your PR.

## Going Deeper
You can learn about the individual tempo plugins in their respective READMEs:<br />
[TempoCore](/TempoCore/README.md)<br />
[TempoSensors](/TempoSensors/README.md)<br />
[TempoAgents](/TempoAgents/README.md)<br />
[TempoGeographic](/TempoGeographic/README.md)<br />
[TempoMovement](/TempoMovement/README.md)<br />
[TempoWorld](/TempoWorld/README.md)<br />

And, if you are using ROS:<br />
[TempoROS](https://github.com/tempo-sim/TempoROS)<br />
[TempoROSBridge](/TempoROSBridge/README.md)<br />
