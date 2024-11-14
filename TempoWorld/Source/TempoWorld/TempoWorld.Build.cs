// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

public class TempoWorld : TempoModuleRules
{
    public TempoWorld(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "Core",
                // Tempo
                "TempoCoreShared",
                "TempoScripting",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "CoreUObject",
                "Engine",
                "MassActors",
                "MassEntity",
                "MassTraffic",
                "StructUtils",
                // Tempo
                "TempoCore",
                "TempoMovementShared",
            }
        );
    }
}
