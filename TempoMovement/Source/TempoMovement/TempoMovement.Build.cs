// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoMovement : TempoModuleRules
{
	public TempoMovement(ReadOnlyTargetRules Target) : base(Target)
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
				// Unreal
				"AIModule",
				"Core",
				"Engine",
				// Tempo
				"TempoCoreShared",
				"TempoScripting",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"NavigationSystem",
				// Tempo
				"TempoMovementShared",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
