// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoAgentsEditor : TempoModuleRules
{
	public TempoAgentsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[]
			{
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[]
			{
			}
			);
			
		
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
				"UnrealEd",
				"MassTraffic",
				"ZoneGraph",
				// Tempo
				"TempoAgentsShared",
				"TempoAgents",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
