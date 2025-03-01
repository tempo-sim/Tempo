﻿// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

public class TempoMovementROSBridge : ModuleRules
{
    public TempoMovementROSBridge(ReadOnlyTargetRules Target) : base(Target)
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
                "TempoMovement",
                "rclcpp",
                "TempoROSBridgeShared",
                "TempoCoreShared",
            }
        );
        
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_FASTRTPS_CPP_BUILDING_DLL_tempo_movement_ros_bridge=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_CPP_BUILDING_DLL=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_BUILDING_DLL=1");
        }
        
        bEnableExceptions = true;
    }
}
