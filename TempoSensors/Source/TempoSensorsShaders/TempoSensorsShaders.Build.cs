// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

// Hosts TempoSensors' global shaders. Global shader types have to be registered, and the plugin's
// shader directory mapped, before the engine compiles the global shader map in PreInit, which is
// why this is a separate module loaded at PostConfigInit rather than part of TempoSensors (Default).
public class TempoSensorsShaders : TempoModuleRules
{
	public TempoSensorsShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"RenderCore",
				"RHI",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Projects",
			}
		);
	}
}
