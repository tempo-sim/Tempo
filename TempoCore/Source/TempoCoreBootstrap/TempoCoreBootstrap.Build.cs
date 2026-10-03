// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;

// Not a TempoModuleRules: this module must load before anything that links gRPC's shared library,
// so it cannot link it itself.
public class TempoCoreBootstrap : ModuleRules
{
	public TempoCoreBootstrap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
			}
		);
	}
}
