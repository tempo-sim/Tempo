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
                "MassCommon",
                "MassTraffic",
                "MassRepresentation",
                "MassEntity",
                "MassActors",
                "MassSpawner",
                "MassCommon",
                "StructUtils",
                "RHI",
                // For overriden "stand" state
                "MassMovement",
                "MassNavigation",
                "MassZoneGraphNavigation",
                "MassAIBehavior",
                "StateTreeModule",
                "MassSignals",
                // Tempo
                "TempoCore",
            }
        );
    }
}
