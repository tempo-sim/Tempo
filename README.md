# Tempo
Tempo is a collection of simulation-focused plugins for Unreal Engine. Tempo makes the power of Unreal accessible to simulation and robotics developers, including plugins for scripting, sensor simulation, agent behaviors, and more.

Tempo is the foundation on which you can build a simulator for your unique application. Not sure where to start? Want some guidance from the authors? Feel free to [reach out](https://www.temposimulation.com/contact).

## Compatibility
- Linux (Ubuntu 22.04 and 24.04), MacOS (13.0 "Ventura" or newer, Apple silicon only), Windows 10 and 11
- Unreal Engine 5.4 and 5.5
> [!WARNING]
> A change in XCode 16.3 broke Unreal builds (not just Tempo). The [fix](https://github.com/EpicGames/UnrealEngine/commit/36e6414349658ce0ef27d3733a764e392b410a7c) will be in 5.6, but we do not know if Epic will make a 5.4 or 5.5 hotfix for it. In the meantime, we recommend dowgrading to Xcode to 16.2 on Mac. You can find previous Xcode releases [here](https://developer.apple.com/download/all/) (you'll need a free Apple developer account).

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
  - [Git Bash](https://gitforwindows.org/) (Run all Tempo `*.sh` scripts from Git Bash)
  - `jq`: (Use Admininistrator Git Bash) `curl -L -o /usr/bin/jq.exe https://github.com/jqlang/jq/releases/latest/download/jq-win64.exe`

## Environment Variables
- `UNREAL_ENGINE_PATH`: Your Unreal Engine installation directory (the folder containing `Engine`)
  - On Linux, unzip where you like, for example `~/UE_5.5`
  - The Mac default is `/Users/Shared/Epic Games/UE_5.5`
  - On Windows the default is `C:\Program Files\Epic Games\UE_5.5`
> [!NOTE]
> If you're using 5.4 make sure to update the paths above accordingly.

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

### Project Changes
The Tempo plugins require one change to a vanilla Unreal Engine project to work properly:
- Your project's `*.Target.cs` files must use the Tempo UnrealBuildTool toolchain for your platform. See [TempoSample.Target.cs](https://github.com/tempo-sim/TempoSample/blob/main/Source/TempoSample.Target.cs) and [TempoSampleEditor.Target.cs](https://github.com/tempo-sim/TempoSample/blob/main/Source/TempoSampleEditor.Target.cs) for examples.

### One-Time Setup
Run the `Setup.sh` script (from the `Tempo` root) once. This script will:
- Install the Tempo Unreal Engine mods (we make some changes to your installed Engine in-place to avoid distributing a custom engine build)
- Download third party dependencies
- Add git hooks to keep both of the above in sync automatically as you check out different Tempo commits

## Using Tempo
### Building, Running, and Packaging
Use `Scripts/Build.sh` to build the project, `Scripts/Run.sh` to run Unreal Editor with the project, and `Scripts/Package.sh` to package the project into a standalone binary.

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
6. Lastly, use TempoTime to pause, resume, and step the simulation:
```
import tempo.tempo_time as tt
import TempoTime.Time_pb2 as Time
tt.pause() # Time should pause
tt.play() # Time should resume
tt.set_time_mode(Time.FIXED_STEP) # Time mode should switch to Fixed Step, simulation should run faster than real-time
tt.step() # Time should advance to the nearest whole number of fixed time steps (0.1 seconds by default)
tt.step() # Time should advance one step. You should get one new camera image every step.
```
Congratulations, you are officially up and running! Continue experimenting with Tempo by creating new scenes, streaming sensor data, and varying the many properties of your simulation at runtime.

### Package
Use the included `Scripts/Package.sh` to package your project into a standalone binary, which you can then run from the `Packaged` folder.

## Continuous Integration
If you would like to set up a GitHub actions pipeline to build, package, run, and/or release your Tempo project, check out the `build_and_package` reusable workflow in [.github/workflows](https://github.com/tempo-sim/Tempo/tree/main/.github/workflows). `TempoSample`'s [tempo_sample_build_and_package](https://github.com/tempo-sim/TempoSample/blob/main/.github/workflows/tempo_sample_build_and_package.yml) workflow is a good reference.

## Getting Help
Stuck on something? Feel free to send us an [issue](https://github.com/tempo-sim/Tempo/issues) or ask a question on our [Discord](https://discord.gg/bKa2hnGYnw).

### Continuous Integration
If you would like to set up a GitHub actions pipeline to build, package, or release your Tempo Unreal project you should use the `build_and_package` reusable workflow in `.github/workflows`. The workflow has a number of configurable inputs, but for a typical Unreal Tempo project the defaults should work fine. [TempoSample](https://github.com/tempo-sim/TempoSample)'s `tempo_sample_build_and_package` workflow is a good example.

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
