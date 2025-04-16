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
				});

			// StructUtils plugin was deprecated in 5.5 and moved into CoreUObject
			if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
			{
				PublicDependencyModuleNames.Add("StructUtils");
			}

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
