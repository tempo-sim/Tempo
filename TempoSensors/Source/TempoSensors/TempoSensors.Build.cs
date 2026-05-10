// Copyright Tempo Simulation, LLC. All Rights Reserved

using System.IO;
using UnrealBuildTool;

public class TempoSensors : TempoModuleRules
{
	public TempoSensors(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Hack to reach private members of FRayTracingScene in TempoSceneCaptureComponent.
		// See comment in UTempoSceneCaptureComponent::UpdateSceneCaptureContents.
		// Also needed in 5.6 to reach FSceneViewState::GetLastAverageSceneLuminance via downcast —
		// the method only became virtual on FSceneViewStateInterface in 5.7. See call site in
		// TempoCamera.cpp.
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Private"));
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Internal"));

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
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
				"DeveloperSettings",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				// AVCodecs (H.264 video encoder for VideoFrame measurements). Vendor backends
				// (NVENC, VTCodecs, AMF, WMF) are loaded via TempoSensors.uplugin's plugin list.
				"AVCodecsCore",
				"AVCodecsCoreRHI",
			}
		);
	}
}
