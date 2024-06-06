using UnrealBuildTool;

public class TempoSensorsShared : TempoModuleRules
{
    public TempoSensorsShared(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Engine",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "CoreUObject",
                "DeveloperSettings",
                "RenderCore",
                "RHI",
                "Slate",
                "SlateCore",
                // Tempo
                "TempoCoreShared",
                "TempoScripting",
            }
        );
    }
}