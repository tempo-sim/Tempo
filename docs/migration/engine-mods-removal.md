# Engine Mods Removal

Tempo no longer modifies your Unreal installation. It builds against the engine exactly as Epic
ships it ([what replaced the mods](../plugins/engine-mods.md)).

Earlier versions of `Setup.sh` made three kinds of change that stay behind until you undo them:

- they added Tempo's own toolchains (`TempoVCToolChain`, `TempoMacToolChain` and
  `TempoLinuxToolChain`) to UnrealBuildTool, rebuilding it;
- they switched your project's `*.Target.cs` files to those toolchains;
- they patched a few of the engine's AutomationTool and `EpicGames.Perforce` project files.

`Setup.sh` no longer does any of that, and it does not undo it.

## What changed at a glance

| Change | Affects | Breaks |
|---|---|---|
| The toolchains are no longer installed in the engine | Every project using Tempo | Nothing, until the engine no longer has them. Then any `*.Target.cs` that still names one fails to build with `Unable to create toolchain 'TempoVCToolChain'` (or the Mac or Linux one). |
| A TempoThirdParty gRPC release without `tempogrpc` is no longer supported | Checkouts whose third-party dependencies are out of date | Build: `tempogrpc ... is missing`. `Scripts/SyncDeps.sh` downloads the right release, and the git hooks run it for you. |
| `Scripts/InstallEngineMods.sh` is gone | The `post-checkout` and `post-merge` git hooks `Setup.sh` installed | Nothing, but each checkout prints that the script is missing until you re-run `Setup.sh` |
| `build_and_package.yml` lost its `engine_mods_image`, `registry`, `registry_username`, `aws_region` and `aws_role_to_assume` inputs, and `publish_engine_mods.yml` and `prune_engine_mods.yml` are gone | CI that used a pre-modded image | The workflow run, until you remove them from your caller |
| TempoROS's copy handler is found and built by AutomationTool itself | Packaging with TempoROS | Nothing. `TempoROS/Scripts/BuildAutomation.sh` is gone, and `-ScriptDir` is no longer needed. |

## What you need to do

### 1. Remove the toolchain block from your `*.Target.cs` files

**Do this first.** An engine without the toolchains cannot build a target that names one.

Running `Scripts/Setup.sh` (step 3) does this for you: it deletes the block the old `Setup.sh`
added to each `*.Target.cs` file in your project's `Source/` folder, from the
`TEMPO_TOOLCHAIN_BLOCK BEGIN` line to the `TEMPO_TOOLCHAIN_BLOCK END` line:

```csharp
		// TEMPO_TOOLCHAIN_BLOCK BEGIN - Added by UseTempoToolChain.sh script
		if (Platform == UnrealTargetPlatform.Win64)
		{
			ToolChainName = "TempoVCToolChain";
		}
		else if (Platform == UnrealTargetPlatform.Mac)
		{
			ToolChainName = "TempoMacToolChain";
		}
		else if (Platform == UnrealTargetPlatform.Linux)
		{
			ToolChainName = "TempoLinuxToolChain";
		}
		// TEMPO_TOOLCHAIN_BLOCK END
```

If you copied that block into a `*.Target.cs` yourself, it may be there without the marker
comments. `Setup.sh` does not edit what it did not write — it warns about any remaining selection
of the three names above, and you remove it yourself.

### 2. Reinstall Unreal Engine

Return the engine to the way Epic ships it by reinstalling it: uninstall and install it again in the
Epic Games Launcher, or extract a fresh copy of the engine on Linux. Do this on every machine and
build agent where you ran `Setup.sh`.

If a `TempoMods` folder is left in the engine's root directory afterwards, delete it.

Until then, Tempo's build warns that the engine still carries the old modifications, and the
compiler warns (`CS0436`) that the modified UnrealBuildTool's `TempoModuleRules` duplicates
Tempo's. Both are harmless: the superseded pieces go unused.

### 3. Run `Setup.sh` again

`Scripts/Setup.sh` removes the toolchain block from your `*.Target.cs` files (step 1), takes
`InstallEngineMods.sh` out of the git hooks it installed, warns if the engine still carries the
mods (step 2), and makes sure your third-party dependencies are up to date.

### 4. Update your CI

If your `build_and_package.yml` caller passes `engine_mods_image` or any of the registry or AWS
inputs above, remove them, along with the `packages: read` permission they needed. Delete any
workflow that calls `publish_engine_mods.yml` or `prune_engine_mods.yml`, and, if you like, the
image package they published. See [Continuous integration](../guides/continuous-integration.md).
