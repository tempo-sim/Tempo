// Copyright Tempo Simulation, LLC. All Rights Reserved

using UnrealBuildTool;

public class TempoWorldEditor : TempoModuleRules
{
	public TempoWorldEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"TempoWorld",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"CoreUObject",
				"Engine",
				"Json",
				// Used directly, for GetSubClassWithName/GetSubClassWithPath: a client-supplied
				// class name must always resolve through those (AGENTS.md section 6).
				"TempoCore",
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
				}
			);
		}
	}
}
