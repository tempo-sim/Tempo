// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

public class TempoWorld : TempoModuleRules
{
	public TempoWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

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
				"MassActors",
				"MassEntity",
				"MassTraffic",
			}
		);

		// StructUtils plugin was deprecated in 5.5 and moved into CoreUObject
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}

		// UE 5.8 split the core Mass types out of the MassEntity plugin into a new MassCore module.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8)
		{
			PrivateDependencyModuleNames.Add("MassCore");
		}
	}
}
