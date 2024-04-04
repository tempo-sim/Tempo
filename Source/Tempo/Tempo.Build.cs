// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class Tempo : ModuleRules
{
	public Tempo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
