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
                "Core",
                "Engine",
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

        // Hack to get access to private members of FRayTracingScene in TempoSceneCaptureComponent.
        // See comment in UTempoSceneCaptureComponent::UpdateSceneCaptureContents for more detail.
        PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Private"));
        PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("Renderer"), "Internal"));
    }
}
