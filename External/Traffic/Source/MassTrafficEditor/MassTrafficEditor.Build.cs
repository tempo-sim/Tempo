// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MassTrafficEditor : ModuleRules
{
	public MassTrafficEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"ZoneGraph",
				"MassTraffic",
			}
			);

		// StructUtils plugin was deprecated in 5.5 and moved into CoreUObject
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Slate",
				"SlateCore",
				"GameplayTasks",
				"MassCommon",
				"MassEntity",
				"RenderCore",
				"RHI",
				"Blutility",
				"PointCloud"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
		
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
				}
			);
		}
	}
}
