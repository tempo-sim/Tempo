using UnrealBuildTool;

public class TempoVehicleMovement : ModuleRules
{
    public TempoVehicleMovement(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "AIModule",
                "Core",
                "ChaosVehicles",
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
                "TempoMovementShared",
            }
        );
    }
}
