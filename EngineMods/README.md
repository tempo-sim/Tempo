# Tempo Engine Mods

The files in this folder and the associated `Scripts/InstallEngineMods.sh` script are intended to make small modifications to a user's local UnrealEngine installation. Doing so allows Tempo to make minor adjustments to the engine in-place without forcing users to install a custom Tempo engine.

Each sub-folder can have a different mechanism for modifying the engine in-place, typically by adding some new file or patching an existing one and then re-building the necessary portion of the engine.

Different versions of each mod may be necessary for different Major.Minor versions of the engine. The `EngineMods.json` file specifies which mod files apply to which versions. Mods will be applied in the order they appear in the `Type`/`Files` pairs in that file, as certain mods may depend on others.

It is unlikely but not impossible that an Unreal hotfix release may break one of these mods. We will address that if it happens.
