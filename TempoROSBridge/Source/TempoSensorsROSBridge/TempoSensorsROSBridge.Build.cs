using UnrealBuildTool;

public class TempoSensorsROSBridge : ModuleRules
{
    public TempoSensorsROSBridge(ReadOnlyTargetRules Target) : base(Target)
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
                "TempoCamera",
                "TempoSensors",
                "TempoSensorsShared",
                "TempoScripting",
                "TempoROS",
                "rclcpp",
                "TempoROSBridgeShared",
                "TempoCoreShared",
            }
        );
        
        bEnableExceptions = true;
    }
}
