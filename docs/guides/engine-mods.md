# Engine Mods

Tempo makes small modifications to your local Unreal Engine installation, in place. That is what
lets Tempo adjust engine behavior without forcing you to build and install a custom Tempo engine.

The mods live in [`EngineMods/`](https://github.com/tempo-sim/Tempo/tree/main/EngineMods) and are
applied by `Scripts/InstallEngineMods.sh`.

## When they are applied

- **On first setup**, by `Setup.sh`.
- **Automatically thereafter**, by the `post-checkout` and `post-merge` git hooks `Setup.sh`
  installs — so moving between Tempo commits keeps your engine in sync.
- **Manually**, with `Scripts/InstallEngineMods.sh`, which you need if you set up with
  `-skip-hooks`.

!!! warning "`-skip-hooks` means you own the syncing"

    `Setup.sh -skip-hooks` is for developers actively working on Tempo itself, where the hooks add
    noticeable overhead to every `git checkout`. Without them, engine mods and third-party deps do
    **not** re-sync when you change Tempo commits — run `Scripts/InstallEngineMods.sh` and
    `Scripts/SyncDeps.sh` yourself.

## How they work

Mods can be **additions**, **patches**, or **removals** of files. Patches can be stacked, and are
applied in the order they appear in each patch list.

Different versions of a mod may be needed for different Major.Minor versions of the engine.
`EngineMods.json` specifies which mod files apply to which engine versions. Mods are applied in
the order they appear in that file, since later mods may depend on earlier ones.

It is unlikely but not impossible that an Unreal hotfix release breaks one of these mods. We'll
address that if it happens.

## Authoring a new patch

Run a diff from the *unmodified* engine folder you want to patch. For example, to generate a patch
for the `Source` folder in an engine plugin:

```bash
cd "$UNREAL_ENGINE_PATH/Engine/Plugins/MyPlugin"
diff -urN --strip-trailing-cr Source <modified_plugin_root>/Source > MyPlugin.patch.1
```

`Scripts/ExtractPatch.sh` and `Scripts/ApplyPatch.sh` wrap the extract/apply steps.

## In CI

Applying engine mods inside every CI run costs ~10–15 minutes. You can publish a pre-modded engine
image to your own private GHCR instead:

[:octicons-arrow-right-24: Pre-modded engine image](continuous-integration.md#speeding-up-builds-with-a-pre-modded-engine-image)

!!! danger "Engine images must stay private"

    A pre-modded image contains UE-derived content, which the Unreal Engine EULA does not permit
    redistributing publicly.
