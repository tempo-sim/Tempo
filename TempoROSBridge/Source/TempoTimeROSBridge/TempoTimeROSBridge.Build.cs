using UnrealBuildTool;

public class TempoTimeROSBridge : ModuleRules
{
    public TempoTimeROSBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
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
                "TempoTime",
                "TempoCoreShared",
                "TempoScriptingROSBridge",
                "TempoROS",
                "rclcpp",
                "TempoScripting",
                "TempoROSBridgeShared",
            }
        );

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_FASTRTPS_CPP_BUILDING_DLL_tempo_time_ros_bridge=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_CPP_BUILDING_DLL=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_BUILDING_DLL=1");
        }

        bEnableExceptions = true;
    }
}
