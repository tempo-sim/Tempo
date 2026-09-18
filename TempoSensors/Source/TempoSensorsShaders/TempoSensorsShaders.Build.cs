// Copyright Tempo Simulation, LLC. All Rights Reserved

using System.IO;
using UnrealBuildTool;

// Hosts TempoSensors' global shaders. Global shader types have to be registered, and the plugin's
// shader directory mapped, before the engine compiles the global shader map in PreInit, which is
// why this is a separate module loaded at PostConfigInit rather than part of TempoSensors (Default).
public class TempoSensorsShaders : TempoModuleRules
{
	public TempoSensorsShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// The lidar's participating media pass binds the renderer's local fog volume parameter struct
		// (LocalFogVolumeRendering.h), which is header-only but private. Headers only: nothing here
		// links against Renderer.
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Private"));

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
