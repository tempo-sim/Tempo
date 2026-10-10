using UnrealBuildTool;

public class TempoROSBridgeShared : TempoModuleRules
{
	public TempoROSBridgeShared(ReadOnlyTargetRules Target) : base(Target)
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
			}
		);

		bEnableExceptions = true;
	}
}
