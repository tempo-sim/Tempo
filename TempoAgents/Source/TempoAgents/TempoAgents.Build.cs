// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoAgents : TempoModuleRules
{
	public TempoAgents(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"Core",
				"Engine",
				"RenderCore",
				"ZoneGraph",
				// Tempo
				"TempoCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"DeveloperSettings",
				"MassActors",
				"MassAIBehavior",
				"MassCommon",
				"MassEntity",
				"MassMovement",
				"MassNavigation",
				"MassRepresentation",
				"MassSignals",
				"MassSimulation",
				"MassSpawner",
				"MassTraffic",
				"MassZoneGraphNavigation",
				"RHI",
				"Slate",
				"SlateCore",
				"StateTreeModule",
			}
		);

		// StructUtils plugin was deprecated in 5.5 and moved into CoreUObject.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
