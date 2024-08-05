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
            }
        );
        
        PublicIncludePaths.Add("$(PluginDir)/../External/Traffic/Source/MassTraffic/Public");

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "DeveloperSettings",
                "Slate",
                "SlateCore",
                "ZoneGraph",
                "MassTraffic",
                "MassRepresentation",
                "MassEntity",
                "StructUtils"
            }
        );
    }
}
