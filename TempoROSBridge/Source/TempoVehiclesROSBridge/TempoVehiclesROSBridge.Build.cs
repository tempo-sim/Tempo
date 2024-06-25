// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

public class TempoVehiclesROSBridge : ModuleRules
{
    public TempoVehiclesROSBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        OptimizeCode = CodeOptimization.Never;
        
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
                "Engine",
                "Slate",
                "SlateCore",
                // Tempo
                "TempoROS",
                "TempoScriptingROSBridge",
                "TempoScripting",
                "TempoVehicles",
                "rclcpp",
                "TempoROSBridgeShared",
                "TempoCoreShared",
            }
        );
        
        bEnableExceptions = true;
    }
}
