// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

public class TempoCoreShared : ModuleRules
{
	public TempoCoreShared(ReadOnlyTargetRules Target) : base(Target)
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
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
