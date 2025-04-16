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
				"MassEntity",
				"MassTraffic",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"ZoneGraph",
				// Tempo
				"TempoAgentsShared",
				"TempoAgents",
				"TempoScripting",
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

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
