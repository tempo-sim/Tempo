// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class TempoCamera : TempoModuleRules
{
	public TempoCamera(ReadOnlyTargetRules Target) : base(Target)
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

		// Needed in 5.6 to reach FSceneViewState::GetLastAverageSceneLuminance via downcast —
		// the method only became virtual on FSceneViewStateInterface in 5.7. See call site in
		// TempoCamera.cpp.
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Private"));
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Internal"));

		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"TempoSensorsShared",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"Engine",
				"RenderCore",
				"RHI",
				// Tempo
				"TempoLabels",
				"TempoScripting",
				"TempoCoreShared",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
