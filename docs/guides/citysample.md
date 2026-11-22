# Tempo + CitySample Quick Start

!!! warning "Work in progress"

    This page is a work in progress. The steps below get a stock CitySample project building and
    running with Tempo, but the integration is still settling and the details here may change.

Epic's **CitySample** — the project behind the *Matrix Awakens* city — is a large, fully authored
urban environment with traffic, crowds and drivable vehicles already in it. It is a natural host
for Tempo: Tempo's [Traffic](../plugins/traffic.md) plugin is a fork of CitySample's own, so the
simulation you get is the one the content was built for.

That shared ancestry is also the thing you have to handle. This page is the short path from a
stock CitySample checkout to a Tempo gRPC server you can drive from Python.

!!! info "Read [Installation](../getting-started/installation.md) too"

    This page covers only what is *different* about CitySample. The general install flow, and what
    `Setup.sh` and `Build.sh` do, are on the [Installation](../getting-started/installation.md)
    page.

## Before you start

- The [prerequisites](../getting-started/prerequisites.md) for your platform — `jq`, and
  `UNREAL_ENGINE_PATH` on Linux.
- A CitySample checkout on **UE 5.7 or 5.8**. Epic distributes CitySample through the Epic Games
  Launcher and Fab. Tempo does not support 5.6 or earlier.

## 1. The CitySample source fixes { #patch-citysample }

You don't need to do anything for this step: `Setup.sh` (step 3) runs `Scripts/PatchCitySample.sh`,
which makes both fixes. They're described here because they edit CitySample's own source. Both bugs
are in CitySample itself, on UE 5.8, with no plugin involved: one stops the build on macOS and
Linux, and the other crashes a packaged game when the player gets into a car.

The script changes only the exact lines it expects, so running it again does nothing. If Epic
changes the code a fix touches, it leaves that file alone and says so.
`Scripts/PatchCitySample.sh -restore` puts Epic's lines back. In any other project it finds no
CitySample source and does nothing.

### The compile error on macOS and Linux { #patch-citysample-compile }

A stock build fails with:

```text
CitySample/Source/CitySample/Util/CitySampleBlueprintLibrary.cpp:203:97: error: loop will run
    at most once (loop increment never executed) [-Werror,-Wunreachable-code-loop-increment]
  203 |  for (TActorIterator<ACitySampleWorldInfo> It(World, ACitySampleWorldInfo::StaticClass()); It; ++It)
      |                                                                                               ^~~~
```

The loop body returns unconditionally, so `++It` can never run. The function means "return the
first `ACitySampleWorldInfo`, or `nullptr`", and the script rewrites that one line to say so:

```cpp title="Source/CitySample/Util/CitySampleBlueprintLibrary.cpp"
ACitySampleWorldInfo* UCitySampleBlueprintLibrary::GetWorldInfo(const UObject* const WorldContextObject)
{
    if (UWorld* const World = WorldContextObject->GetWorld())
    {
        if (TActorIterator<ACitySampleWorldInfo> It(World, ACitySampleWorldInfo::StaticClass()); It)
        {
            return *It;
        }
    }

    return nullptr;
}
```

This is exactly equivalent. `TActorIteratorBase` has an `explicit operator bool()`, so `if (It)`
performs the same validity check the `for` condition did, and constructing the iterator already
advances it to the first match. CitySample is built as C++20 — `BuildSettingsVersion.V4` and later
set `CppStandardVersion.Cpp20` — so the if-with-initializer form is available.

??? question "Why doesn't this happen in other projects?"

    All three CitySample targets set `DefaultBuildSettings = BuildSettingsVersion.V7`. `V7` is new
    in UE 5.8, and one of the three defaults it changes is:

    > `ModuleRules.CppCompileWarningSettings.UnreachableCodeWarningLevel = WarningLevel.Error` —
    > Enables compile-time validation of unreachable code. (Previously: Error for MSVC, Off for
    > Clang.)

    On Clang this warning was previously off entirely. At `V7`, UnrealBuildTool passes
    `-Wunreachable-code-aggressive`, which is a group covering four diagnostics:

    | Diagnostic | Fires on |
    |---|---|
    | `-Wunreachable-code` | Any statement that control flow cannot reach |
    | `-Wunreachable-code-break` | A `break` after a `return`, `continue` or `throw` |
    | `-Wunreachable-code-loop-increment` | A loop whose body always exits — the case above |
    | `-Wunreachable-code-return` | A `return` that cannot be reached |

    A project pinned to `V6` or lower, or one whose targets never set `DefaultBuildSettings` at
    all, does not get any of them on Clang. That is the whole difference.

    This propagates into plugins, too: `ModuleRules.DefaultBuildSettings` falls back to the
    target's, so **every plugin in a CitySample project is compiled at V7** unless it overrides
    the setting itself. Tempo's modules are clean at V7, but your own may not be.

!!! tip "Relaxing the warning instead"

    If you would rather not patch this line, a module can opt out in its `*.Build.cs`:

    ```csharp
    CppCompileWarningSettings.UnreachableCodeWarningLevel = WarningLevel.Warning;
    ```

    Dropping the whole project to `BuildSettingsVersion.V6` also works, but it switches the
    warning off for every plugin in the project — including yours.

### The crash when entering a vehicle { #patch-citysample-vehicle-entry }

A packaged game crashes the moment the player character gets into a car. It happens on every
platform, and **never in the editor**, so you won't see it until you package:

```text
Script Stack (3 frames) :
/Game/Gameplay/Driving/BP_CarEntryInteraction.BP_CarEntryInteraction_C.ExecuteUbergraph_BP_CarEntryInteraction
/Game/Gameplay/Driving/BP_CarEntryInteraction.BP_CarEntryInteraction_C.OnNotifyBegin_F4B523ED48E3799F950FDEA6575EEDC5
/Script/AnimGraphRuntime.PlayMontageCallbackProxy.OnNotifyBeginReceived

LogWindows: Error: appError called: Assertion failed: IsProcessing() == false
    [File:...\Engine\Source\Runtime\MassEntity\Private\MassEntityManager.cpp] [Line: 1406]
Synchronous API function FMassEntityManager::AddTagToEntity called during mass processing.
Use asynchronous API instead.
```

When the car-entry montage reaches its notify, CitySample possesses the vehicle.
`ACitySampleVehicleBase::PossessedBy` then moves `FMassTrafficPlayerVehicleTag` from the previous
player vehicle's Mass entity to this one. It does that with Mass's synchronous API
(`RemoveTagFromEntity`, and `UE::MassActor::AddEntityTagToActor`, which calls `AddTagToEntity`).
UE 5.8 asserts on those calls whenever Mass is processing.

The script changes both calls to queue the change while Mass is processing, and to make it at once
otherwise, as before. In `PossessedBy`, the block from `const UWorld* World = GetWorld();` up to
`CitySamplePlayerController->SetCurrentPlayerVehicle(this);` becomes:

```cpp title="Source/CitySample/Vehicles/CitySampleVehicleBase.cpp"
		const UWorld* World = GetWorld();
		{
			// When removing this player vehicle tag whe have to do it this slightly more complex way
			// instead of UE::MassActor::RemoveEntityTagFromActor<FTagFragment_MassTrafficPlayerVehicle>(*PreviousPlayerVehicle);
			// because the actor might not actually exist any more. This entity might be represented by an ISMC or something
			// else. By using the handle we know we're changing the Mass entity no matter what it is now.
			const FMassEntityHandle & MassAgentHandle = CitySamplePlayerController->GetCurrentPlayerVehicleMassHandle();
			if (MassAgentHandle.IsValid())
			{
				// An entity might be removed from Mass (because the vehicle was damaged and LODs out), hence
				// the thorough IsEntityValid() check.
				UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(World);
				if (EntitySubsystem && EntitySubsystem->GetMutableEntityManager().IsEntityValid(MassAgentHandle))
				{
					FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
					// PossessedBy can run while a Mass phase is still in flight (in UE 5.8 game builds, phases
					// don't block the game thread), where synchronous composition changes assert. Defer in that case.
					if (EntityManager.IsProcessing())
					{
						EntityManager.Defer().RemoveTag<FMassTrafficPlayerVehicleTag>(MassAgentHandle);
					}
					else
					{
						EntityManager.RemoveTagFromEntity(MassAgentHandle, FMassTrafficPlayerVehicleTag::StaticStruct());
					}
				}
			}
		}

		UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(World);
		UMassActorSubsystem* MassActorSubsystem = UWorld::GetSubsystem<UMassActorSubsystem>(World);
		if (EntitySubsystem && MassActorSubsystem)
		{
			const FMassEntityHandle MassAgentHandle = MassActorSubsystem->GetEntityHandleFromActor(this);
			if (MassAgentHandle.IsValid())
			{
				// Not using UE::MassActor::AddEntityTagToActor here: it calls the synchronous
				// AddTagToEntity, which asserts if this runs during Mass processing.
				FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
				if (EntityManager.IsProcessing())
				{
					EntityManager.Defer().AddTag<FMassTrafficPlayerVehicleTag>(MassAgentHandle);
				}
				else
				{
					EntityManager.AddTagToEntity(MassAgentHandle, FMassTrafficPlayerVehicleTag::StaticStruct());
				}
			}
			else
			{
				UE_LOG(LogCitySample, Verbose, TEXT("MassAgentHandle not valid for vehicle. Mass AI spawner may not be setup for current level. Vehicle = %s"), *GetName());
			}
		}
```

It also adds `#include "MassCommandBuffer.h"` before the file's `#include "MassEntityManager.h"`.

The only change in behavior is timing. A queued tag lands when the current Mass phase finishes,
not at once. Mass merges and applies commands queued on the entity manager when a phase cleans up,
so code that reads `FMassTrafficPlayerVehicleTag` later in the same phase won't see it yet.

??? question "Why does Mass count as processing here?"

    The crash's callstack has no Mass code on it. The notify fires from the player character's
    ordinary `CharacterMovementComponent` tick, and no other thread is running a Mass processor at
    that moment. Mass is still *considered* to be processing, though.

    UE 5.8 runs each Mass phase through a new processing queue, which is on by default
    (`mass.UseProcessingQueue`, `MassProcessingPhaseManager.cpp`). In a game build it runs
    **without blocking the game thread**. It starts the phase's work and returns, keeping a
    processing scope open (`FProcessingQueueAsyncCleanup` in `MassProcessingQueue.cpp`) until a
    cleanup task later runs on the game thread. Until then, every actor tick on the game thread
    sees `IsProcessing() == true`. Any synchronous Mass call made from gameplay code in that window
    asserts, and the assert protects against a real data race with workers that may still be
    running.

    The editor forces the queue to block until processing completes (the `WITH_EDITOR` branch in
    `FProcessingQueue::Execute_GetEventRef`), and that covers PIE too. That's why only packaged
    builds crash.

!!! tip "Switching the queue off instead"

    Setting `mass.UseProcessingQueue=0` under `[SystemSettings]` in `Config/DefaultEngine.ini` goes
    back to Mass's previous scheduling, and the crash goes away without a source change. It changes
    how every Mass phase in the project runs, though, just to work around one call site.

## 2. Add Tempo

```bash
cd <your_citysample>/Plugins
git clone --recurse-submodules https://github.com/tempo-sim/Tempo.git
```

This works whether or not your CitySample project is itself a git repository — a plain download
from the Epic Games Launcher is not one. Don't drop `--recurse-submodules`: Tempo nests
`TempoROS`, which otherwise clones as an empty directory.

## 3. Run Setup.sh

```bash
Plugins/Tempo/Setup.sh    # Setup.bat on Windows
```

On a CitySample project the step that matters most is the first one: CitySample ships its own
`Traffic` and `RuleProcessor` plugins, and Tempo ships forks with the same names. Unreal cannot
see both. `Setup.sh` renames the host's descriptors:

```text
Plugins/Traffic/Traffic.uplugin              → Traffic.uplugin.disabled-by-tempo
Plugins/RuleProcessor/RuleProcessor.uplugin  → RuleProcessor.uplugin.disabled-by-tempo
```

`Scripts/DisableConflictingPlugins.sh -restore` puts them back. If you skip this step the build fails in C# before it compiles any C++, with
[`CS0101: already contains a definition for 'MassTraffic'`](troubleshooting.md#cs0101-already-contains-a-definition-for-masstraffic).

!!! note "Your `.uproject` does not need editing"

    CitySample's `Plugins` list already enables `Traffic` **by name**, and after this step that
    name resolves to Tempo's fork — its content still mounts at `/Traffic/`, and `CoreRedirects`
    keep CitySample's assets loading. Tempo's own plugins do not appear in `CitySample.uproject`
    at all and do not need to: a project plugin with no `EnabledByDefault` field is enabled by
    default.

    For the full mechanism, see
    [Traffic as a drop-in replacement](../plugins/traffic.md#drop-in-replacement-for-citysamples-traffic).

It also adds the project settings Tempo needs and CitySample doesn't have, through
`Scripts/ConfigureProject.sh`. They go in a marker-delimited block at the end of
`Config/DefaultEngine.ini`, which overrides CitySample's own values without editing them:

```ini
[/Script/Engine.Engine]
WorldSettingsClassName=/Script/TempoCore.TempoWorldSettings
[/Script/Engine.PhysicsSettings]
bSubstepping=True
MaxSubstepDeltaTime=0.016667
MaxSubsteps=6
[ConsoleVariables]
FastGeo.Enable=False
```

- **Tempo's world settings.** Pausing, stepping and Fixed Step all live on `ATempoWorldSettings`.
  CitySample's levels were saved with the engine's own world settings, and the cook replaces them
  with the configured class, copying their properties across.
- **Physics substepping.** A Fixed Step frame can be a tenth of a second. Without substepping, Chaos
  clamps each frame to 1/30 s, and vehicles would cover a third of the distance the timestamps say.
- **No FastGeo.** In 5.6+ CitySample cooks most of its static city through Epic's FastGeoStreaming
  plugin, which turns actors into lightweight primitives that are not components. Tempo's labeler
  works by setting each component's custom-depth stencil, so it cannot label them. They also don't
  draw into custom depth at all, so whatever labeled object stands behind one shows through it in
  the label images and lidar labels. Without FastGeo, the level cooks as ordinary actors, as
  CitySample shipped before 5.6, at some cost in streaming performance. `Setup.sh -keep-fastgeo`
  skips this one, if you don't need labels.

All three take effect at the next package. `Scripts/ConfigureProject.sh -restore` removes the block.

It also makes the [source fixes](#patch-citysample) from step 1, and downloads third-party
dependencies.

## 4. Build and run

```bash
Plugins/Tempo/Scripts/Build.sh    # Build.bat on Windows
Plugins/Tempo/Scripts/Run.sh      # opens CitySample in Unreal Editor
```

## 5. Verify

Press Play. The Editor log should contain:

```text
LogTempoCore: Display: Tempo gRPC server listening on 0.0.0.0:10001
```

From another terminal:

```bash
source ./TempoEnv/bin/activate            # TempoEnv/Scripts/activate on Windows
python -c "import tempo_sim.tempo_core as tc; print(tc.get_current_level_name())"
```

If that prints the level name, you are ready for
[Hello World](../getting-started/hello-world.md).

## 6. Ride along: the AV sensor example { #av-sensor-example }

`ExampleClients/Python/CitySampleAV` is a turnkey synthetic-sensor example for this project. It
adopts a car Mass traffic is already driving, mounts a JSON-described lidar, pinhole camera and
fisheye camera on it through the [reflected API](reflected-api.md), labels the city, and shows it
all in Rerun, optionally recording a dataset. Package first, under Git Bash on Windows, because the
reflected API is generated at package time:

```bash
Plugins/Tempo/Scripts/Package.sh
pip install Packaged/API/Python/tempo-sim/*.whl Packaged/API/Python/city-sample/*.whl
pip install -r Plugins/Tempo/ExampleClients/Python/CitySampleAV/requirements.txt
python Plugins/Tempo/ExampleClients/Python/CitySampleAV.py --launch Packaged/Windows/CitySample.exe
```

`--launch` starts the game with `-RenderOffScreen`, so it opens no window: the sensors render
offscreen and need none. To start the game yourself, pass the same flag
(`Packaged/Windows/CitySample.exe -DisableSandboxIntro -RenderOffScreen`) and leave out `--launch`.

Its [README](https://github.com/tempo-sim/Tempo/tree/main/ExampleClients/Python/CitySampleAV)
covers choosing vehicles, the rig and label JSON, and the recording format.

## What changed in your project

| | |
|---|---|
| `Source/CitySample/Util/CitySampleBlueprintLibrary.cpp` | One line patched by `Setup.sh` (step 1) |
| `Source/CitySample/Vehicles/CitySampleVehicleBase.cpp` | `PossessedBy` and one `#include` patched by `Setup.sh` (step 1) |
| `Plugins/Traffic/`, `Plugins/RuleProcessor/` | Descriptors renamed `*.disabled-by-tempo` |
| `Config/DefaultEngine.ini` | Tempo's world settings, physics substepping, FastGeo off (a `TEMPO_PROJECT_CONFIG` block at the end) |
| `TempoEnv/` | Generated Python virtual environment with the client packages |

## See also

- [Traffic](../plugins/traffic.md) — what Tempo's fork adds over CitySample's plugin, and how it
  stands in for it
- [TempoAgents](../plugins/tempo-agents.md) — the gRPC surface over the road network and traffic
  controls
- [Troubleshooting](troubleshooting.md) — known failure modes, including the plugin collision
  above
