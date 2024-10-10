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
        
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_FASTRTPS_CPP_BUILDING_DLL_tempo_sensors_ros_bridge=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_CPP_BUILDING_DLL=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_BUILDING_DLL=1");
        }
        
        bEnableExceptions = true;
    }
}
