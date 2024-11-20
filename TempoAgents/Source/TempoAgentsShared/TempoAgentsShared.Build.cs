// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoAgentsShared : ModuleRules
{
    public TempoAgentsShared(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Engine",
                "RenderCore",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "CoreUObject",
                "DeveloperSettings",
                "Slate",
                "SlateCore",
                "ZoneGraph",
                "MassTraffic",
                "MassRepresentation",
                "MassEntity",
                "MassActors",
                "MassSpawner",
                "StructUtils",
                "RHI",
                // Tempo
                "TempoCore",
            }
        );
    }
}
