using UnrealBuildTool;

public class TempoCoreROSBridge : ModuleRules
{
	public TempoCoreROSBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		OptimizeCode = CodeOptimization.Never;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"Core",
				// Tempo
				"TempoCore",
				"TempoROS",
				"rclcpp",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"TempoROSBridgeShared",
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_FASTRTPS_CPP_BUILDING_DLL_tempo_core_ros_bridge=1");
			PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_CPP_BUILDING_DLL=1");
			PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_BUILDING_DLL=1");
		}

		bEnableExceptions = true;
	}
}
