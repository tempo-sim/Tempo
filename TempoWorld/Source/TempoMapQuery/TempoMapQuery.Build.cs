// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

public class TempoMapQuery : TempoModuleRules
{
	public TempoMapQuery(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"Core",
				// Tempo
				"TempoCoreShared",
				"TempoScripting",
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"Engine",
				"MassEntity",
				"Slate",
				"SlateCore",
				// Tempo
				"MassTraffic",
				"ZoneGraph",
			}
			);

		// StructUtils plugin was deprecated in 5.5 and moved into CoreUObject
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
