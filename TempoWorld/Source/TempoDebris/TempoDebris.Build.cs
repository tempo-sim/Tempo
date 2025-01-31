using UnrealBuildTool;

public class TempoDebris : ModuleRules
{
    public TempoDebris(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "CoreUObject",
                "Engine",
                "Foliage",
                "PCG",
                "Slate",
                "SlateCore",
                // Tempo
                "TempoCoreShared",
            }
        );
    }
}
