using UnrealBuildTool;

public class TempoObservableEvents : TempoModuleRules
{
    public TempoObservableEvents(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "Core",
                // Tempo
                "TempoScripting",
                "TempoCoreShared",
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
                "TempoCore"
            }
        );
    }
}
