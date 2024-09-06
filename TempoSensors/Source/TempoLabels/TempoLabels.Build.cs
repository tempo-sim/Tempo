using UnrealBuildTool;

public class TempoLabels : ModuleRules
{
    public TempoLabels(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "Core",
                // Tempo
                "TempoCore",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                // Tempo
                "TempoCoreShared",
                "TempoSensorsShared",
            }
        );
    }
}
