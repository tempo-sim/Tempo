# Tempo + CitySample Quick Start

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

## 1. Patch CitySample so it compiles { #patch-citysample }

Do this first, before adding Tempo — it is a problem in CitySample's own code, and it will fail a
stock build on macOS or Linux with no plugin involved:

```text
CitySample/Source/CitySample/Util/CitySampleBlueprintLibrary.cpp:203:97: error: loop will run
    at most once (loop increment never executed) [-Werror,-Wunreachable-code-loop-increment]
  203 |  for (TActorIterator<ACitySampleWorldInfo> It(World, ACitySampleWorldInfo::StaticClass()); It; ++It)
      |                                                                                               ^~~~
```

The loop body returns unconditionally, so `++It` can never run. The function means "return the
first `ACitySampleWorldInfo`, or `nullptr`" — write that directly:

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

    If you would rather not patch CitySample, a module can opt out in its `*.Build.cs`:

    ```csharp
    CppCompileWarningSettings.UnreachableCodeWarningLevel = WarningLevel.Warning;
    ```

    Dropping the whole project to `BuildSettingsVersion.V6` also works, but it switches the
    warning off for every plugin in the project — including yours.

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

Nothing else on disk is touched, and `Scripts/DisableConflictingPlugins.sh -restore` puts them
back. If you skip this step the build fails in C# before it compiles any C++, with
[`CS0101: already contains a definition for 'MassTraffic'`](troubleshooting.md#cs0101-already-contains-a-definition-for-masstraffic).

!!! note "Your `.uproject` does not need editing"

    CitySample's `Plugins` list already enables `Traffic` **by name**, and after this step that
    name resolves to Tempo's fork — its content still mounts at `/Traffic/`, and `CoreRedirects`
    keep CitySample's assets loading. Tempo's own plugins do not appear in `CitySample.uproject`
    at all and do not need to: a project plugin with no `EnabledByDefault` field is enabled by
    default.

    For the full mechanism, see
    [Traffic as a drop-in replacement](../plugins/traffic.md#drop-in-replacement-for-citysamples-traffic).

`Setup.sh` also rewrites CitySample's three `*.Target.cs` files to use Tempo's toolchain, installs
the [engine mods](engine-mods.md), and downloads third-party dependencies.

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

## What changed in your project

| | |
|---|---|
| `Source/CitySample/Util/CitySampleBlueprintLibrary.cpp` | Patched by you, in step 1 |
| `Plugins/Traffic/`, `Plugins/RuleProcessor/` | Descriptors renamed `*.disabled-by-tempo` |
| `Source/*.Target.cs` | A `TEMPO_TOOLCHAIN_BLOCK` selecting Tempo's toolchain |
| Your Unreal installation | [Engine mods](engine-mods.md) applied in place |
| `TempoEnv/` | Generated Python virtual environment with the client packages |

## See also

- [Traffic](../plugins/traffic.md) — what Tempo's fork adds over CitySample's plugin, and how it
  stands in for it
- [TempoAgents](../plugins/tempo-agents.md) — the gRPC surface over the road network and traffic
  controls
- [Troubleshooting](troubleshooting.md) — known failure modes, including the plugin collision
  above
