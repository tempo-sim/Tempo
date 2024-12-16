# Tempo Engine Mods

The files in this folder and the associated `Scripts/InstallEngineMods.sh` script are intended to make small modifications to a user's local UnrealEngine installation. Doing so allows Tempo to make minor adjustments to the engine in-place without forcing users to install a custom Tempo engine.

Mods can be in the form of additions, patches, or removals of files. Patches can be stacked, and will be applied in the order they appear in each patch list.

Different versions of each mod may be necessary for different Major.Minor versions of the engine. The `EngineMods.json` file specifies which mod files apply to which versions. Mods will be applied in the order they appear in that file, as later mods may depend on earlier ones.

It is unlikely but not impossible that an Unreal hotfix release may break one of these mods. We will address that if it happens.

> [!Note]
> To generate a new patch, run a diff command from the unmodified engine folder you want to patch. For example, to generate a patch for the `Source` folder in the `MyPlugin` directory:
> 
> ```
> cd $UNREAL_ENGINE_PATH/Engine/Plugins/MyPlugin
> diff -urN --strip-trailing-cr Source <modified_plugin_root>/Source > MyPlugin.patch.1
```
