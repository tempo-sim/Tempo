using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class TempoSensorsShared : TempoModuleRules
{
    public TempoSensorsShared(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                // Unreal
                "Core",
                "Engine",
                // Tempo
                "TempoCoreShared",
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
                // Tempo
                "TempoCoreShared",
                "TempoScripting",
            }
        );
        // Intentionally NOT adding "Renderer" as a module dep: TempoMultiViewCapture.cpp and
        // TempoSceneCaptureComponent2D.cpp both need to reach renderer-private headers
        // (SceneRendering.h and RayTracing/RayTracingScene.h), but only via includes wrapped in
        // `#define private public`. The TempoMultiViewCapture helper calls every RENDERER_API
        // symbol through virtual dispatch on pointers returned by ENGINE_API
        // ISceneRenderBuilder::Create, so no link-time dependency on Renderer is needed.

        // Hack to get access to private members of FRayTracingScene in TempoSceneCaptureComponent.
        // See comment in UTempoSceneCaptureComponent::UpdateSceneCaptureContents for more detail.
        PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Private"));
        PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Internal"));
    }
}
