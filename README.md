# Tempo

https://github.com/user-attachments/assets/dfc7b28b-3b73-4603-a779-dd6e5b2acec9

Tempo is a collection of simulation-focused plugins for Unreal Engine. Tempo makes the power of Unreal accessible to simulation and robotics developers, including plugins for client APIs, sensor simulation, agent behaviors, and more.

Tempo is the foundation on which you can build a simulator for your unique application. Not sure where to start? Want some guidance from the authors? Find us on [![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/bKa2hnGYnw)

## Compatibility
- Linux (Ubuntu 22.04 and 24.04), MacOS (13.0 "Ventura" or newer, Apple silicon only), Windows 10 and 11
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
Run the `Setup.sh` (or `Setup.bat` on Windows) script (from the `Tempo` root) once. This script will:
- Modify your project's `*.Target.cs` files to use Tempo's custom toolchain, which is necessary for linking certain third party dependencies properly
- Install the Tempo Unreal Engine mods, making some changes to your installed Engine in-place
- Download third party dependencies
- Add git hooks to keep engine mods and third party dependencies up to date automatically as you check out different Tempo commits

### Build and Run
Use the included `Scripts/Build.sh` and `Scripts/Run.sh` (or their `.bat` counterparts on Windows) to build your project and open it in Unreal Editor.

### Hello World
> [!NOTE]
> You don't have to install any Python package or dependences to use Tempo. The build step automatically generated a `tempo` Python package and virtual environment, which will be used below.
1. With your Tempo project open in Unreal Editor, from the root of your project, activate the Tempo virtual environment (`source ./TempoEnv/bin/activate` on Linux & Mac, or `source ./TempoEnv/Scripts/activate` on Windows)
2. Start the Python interpreter (`python` on Linux & Mac, or `winpty python` on Windows) and use the Tempo API to start the simulation:
```
import tempo.tempo_core_editor as tce
tce.play_in_editor() # Simulation should begin
```
3. Use TempoWorld to add an Actor to your scene:
```
import tempo.tempo_world as tw
tw.spawn_actor(type="BP_SensorRig") # An Actor with a tripod mesh should appear. It has a TempoCamera on top (although it may not be visible).
```
4. From another terminal, run the included [SensorPlayground](https://github.com/tempo-sim/Tempo/blob/main/ExampleClients/SensorPlayground.py) example client: `python ./ExampleClients/SensorPlayground.py`. Use it to start streaming color images from the `TempoCamera`
5. While streaming images, from the Python interpreter again, use TempoWorld to change one of the Camera's properties:
```
tw.set_float_property(actor="BP_SensorRig", component="TempoCamera", property="FOVAngle", value=60.0) # The field of view of your streaming images should decrease
```
6. Lastly, use TempoCore to pause, resume, and step the simulation:
```
import tempo.tempo_core as tc
import TempoCore.Time_pb2 as Time
tc.pause() # Time should pause
tc.play() # Time should resume
tc.set_time_mode(Time.TM_FIXED_STEP) # Time mode should switch to Fixed Step, simulation should run faster than real-time
tc.step() # Time should advance to the nearest whole number of fixed time steps (0.1 seconds by default)
tc.step() # Time should advance one step. You should get one new camera image every step.
```
Congratulations, you are officially up and running! Continue experimenting with Tempo by creating new scenes, streaming sensor data, and varying the many properties of your simulation at runtime.

### Package
Use the included `Scripts/Package.sh` (or `Package.bat` on Windows) to package your project into a standalone binary, which you can then run from the `Packaged` folder.

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
