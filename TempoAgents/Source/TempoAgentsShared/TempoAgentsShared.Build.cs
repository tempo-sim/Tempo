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
                "ZoneGraph",
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
                "MassCommon",
                "MassTraffic",
                "MassRepresentation",
                "MassActors",
                "MassSpawner",
                "MassCommon",
                "RHI",
                // For overriden "stand" state
                "MassEntity",
                "MassMovement",
                "MassNavigation",
                "MassZoneGraphNavigation",
                "MassAIBehavior",
                "StateTreeModule",
                "MassSignals",
                "MassSimulation",
                // Tempo
                "TempoCore",
                "TempoCoreShared",
            }
        );
        
        if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
        {
            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "StructUtils",
                }
            );
        }
    }
}
