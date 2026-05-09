// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoMovement : TempoModuleRules
{
	public TempoMovement(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"AIModule",
				"ChaosVehicles",
				"Core",
				"Engine",
				// Tempo
				"TempoCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"NavigationSystem",
				"Slate",
				"SlateCore",
			}
		);
	}
}
