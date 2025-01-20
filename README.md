# Tempo
The Tempo Unreal Engine plugins. The Tempo plugins are a set of modular building blocks for creating simulation applications with Unreal Engine.

Creating a single simulator product that can handle any domain is a daunting challenge. But different simulators still share a lot of fundamental pieces. Tempo is the foundation on which you can build your own simulator for your unique application. Not sure where to start? Want some guidance from the authors? Visit us at [temposimulation.com](https://temposimulation.com).

Tempo is a collection of Unreal Engine plugins and is intended to be a submodule of an Unreal project, within the `Plugins` directory.

## Supported Platforms
- Ubuntu 22.04
- MacOS 13.0 (Ventura) or newer, Apple silicon only
- Windows 10 and 11

## Prerequisites
You will need the following:
- Unreal Engine 5.4 installed through the Epic Games Launcher (or downloaded from [here](https://www.unrealengine.com/en-US/linux) on Linux).
- `python(>=3.9)` and `pip`
- `jq` and `curl`
- (optional, only if cross compiling for Linux from Windows) [Linux Cross-Compile Toolchain](https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-requirements-for-unreal-engine?application_version=5.4)
> [!Note]
> On Windows we recommend installing Python through the Microsoft Store since it will also install `pip`, set up the `python` and `python3` aliases correctly, and add everything to your `PATH`. Note that you can install a specific Python version through the Microsoft Store by searching for it.

## Environment Variables
- `UNREAL_ENGINE_PATH`: Your Unreal Engine installation directory (the folder containing `Engine`)
  - On Mac Epic Games Launcher will install to `/Users/Shared/Epic Games/UE_5.4`
  - On Windows Epic Games Launcher will install to `C:\Program Files\Epic Games\UE_5.4`
  - On Linux you choose where to install. `~/UE_5.4` is recommended.
- (Optional, Windows only) `LINUX_MULTIARCH_ROOT`: The extracted toolchain directory (for example `C:\UnrealToolchains\v22_clang-16.0.6-centos7`)

## Getting Started
### TempoSample
The [TempoSample](https://github.com/tempo-sim/TempoSample) project is provided as a reference for recommended settings, convenient scripts, code examples, and project organization. If you are starting from scratch, you might consider creating your repo using `TempoSample` as a "template" repo, and renaming the project.

### Clone Tempo
From your project's Plugins directory:<br />
`git submodule add https://github.com/tempo-sim/Tempo.git`<br />
`git submodule update --init --recursive`

### Project Changes
The Tempo plugins require one change to a vanilla Unreal Engine project to work properly:
- Your project's `*.Target.cs` files must use the Tempo UnrealBuildTool toolchain for your platform. See [TempoSample.Target.cs](https://github.com/tempo-sim/TempoSample/blob/main/Source/TempoSample.Target.cs) and [TempoSampleEditor.Target.cs](https://github.com/tempo-sim/TempoSample/blob/main/Source/TempoSampleEditor.Target.cs) for examples.

### First-Time Setup
Run the `Setup.sh` script (from the `Tempo` root) once. This script will:
- Install the Tempo Unreal Engine mods (we make some small changes to your installed Engine in-place to avoid distributing a custom engine build)
- Install third party dependencies
- Add git hooks to keep both of the above in sync automatically as you check out different Tempo commits
> [!Note]
> If you run `Setup.sh` again it shouldn't do anything, because it can tell it's already run. If something goes wrong you can force it to run again with the `-force` flag.

## Using Tempo
### Building, Running, and Packaging
Use `Scripts/Build.sh` to build the project, `Scripts/Run.sh` to run Unreal Editor with the project, and `Scripts/Package.sh` to package the project into a standalone binary.

### Configuring
Tempo has a number of user-configurable settings. The [TempoSample](https://github.com/tempo-sim/TempoSample) project has our recommended settings. These can be edited through the Unreal Editor project settings and are stored in config files with an "ini" extension. Settings can also be changed in the packaged binary.
> [!Warning]
> Config ini files can not be edited while the packaged binary is running or the project is open in Unreal Editor.

### Debugging
Unreal projects write logs while running. These are a great starting point for debugging.
When running in Unreal Editor you can see the logs in the [Output Log](https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine) window.
The packaged binary will write logs to `<packaged_game_root>/ProjectName/Saved/Logs`

### Continuous Integration
If you would like to set up a GitHub actions pipeline to build, package, or release your Tempo Unreal project you should use the `build_and_package` reusable workflow in `.github/workflows`. The workflow has a number of configurable inputs, but for a typical Unreal Tempo project the defaults should work fine. [TempoSample](https://github.com/tempo-sim/TempoSample)'s `tempo_sample_build_and_package` workflow is a good example.

## Giving Back
Want to contribute to Tempo? Feel free to send us an issue or open a pull request.

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
