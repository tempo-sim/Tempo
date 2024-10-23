using UnrealBuildTool;

public class TempoLidar : TempoModuleRules
{
    public TempoLidar(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "TempoSensorsShared",
                "TempoScripting",
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
                "RenderCore",
                "RHI",
                // Tempo
                "TempoLabels",
                "TempoScripting",
                "TempoCoreShared",
            }
        );
    }
}
