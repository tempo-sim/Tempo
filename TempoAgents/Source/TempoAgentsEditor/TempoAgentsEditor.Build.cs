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
				"EditorFramework",
				"InputCore",
				"Projects",
				"UnrealEd",
				"MassTraffic",
				"MassEntity",
				"Slate",
				"SlateCore",
				"StructUtils",
				"ToolMenus",
				"ZoneGraph",
				// Tempo
				"TempoAgentsShared",
				"TempoAgents",
				"TempoScripting",
				"TempoCoreShared",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
