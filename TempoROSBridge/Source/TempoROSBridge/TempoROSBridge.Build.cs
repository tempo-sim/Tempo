// Copyright Tempo Simulation, LLC. All Rights Reserved

using System.IO;
using UnrealBuildTool;

public class TempoROSBridge : ModuleRules
{
	public TempoROSBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		OptimizeCode = CodeOptimization.Never;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"Core",
				// Tempo
				"TempoSensorsROSBridge",
				"TempoGeographicROSBridge",
				"TempoTimeROSBridge",
				"TempoMovementROSBridge",
				"TempoScriptingROSBridge",
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
			}
			);

		bEnableExceptions = true;
	}
}
