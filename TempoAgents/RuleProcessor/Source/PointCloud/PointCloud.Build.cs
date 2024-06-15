// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PointCloud : ModuleRules
	{
		public PointCloud(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"Projects",
					"Engine",
					"CoreUObject",
					"GeometryCollectionEngine",
					"SQLiteCore",
					"StructUtils"
				});

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{					
						"AlembicLib",
						"SQLiteCore",
					}
				);
			}

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"SourceControl",
					}
				);
			}

			PrivateIncludePaths.AddRange(
				new string[] {
					//"Runtime/PointCloud/Private",					
				});
		}
	}
}
