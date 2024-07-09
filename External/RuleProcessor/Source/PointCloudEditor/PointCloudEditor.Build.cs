// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PointCloudEditor : ModuleRules
{
	public PointCloudEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"MainFrame",
			});
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"PointCloudEditor/Private",
				"PointCloudEditor/Private/AssetTools",
				"PointCloudEditor/Private/Blueprints",
				"PointCloudEditor/Private/Factories",
				"PointCloudEditor/Private/Rules",
				"PointCloudEditor/Private/Shared",
				"PointCloudEditor/Private/Styles",
				"PointCloudEditor/Private/Toolkits",
				"PointCloudEditor/Private/Widgets",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AlembicLib",
				"ApplicationCore",
				"AssetTools",
				"Blutility",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"DataLayerEditor",
				"DesktopWidgets",
				"EditorStyle",
				"EditorSubsystem",
				"Engine",
				"InputCore",
				"Kismet",
				"Niagara", 
				"PointCloud",
				"Projects",
				"SQLiteCore",
				"Slate",
				"SlateCore",
				"SourceControl",
				"ToolMenus",
				"UMG",
				"UnrealEd",
				"ContentBrowserData",
			});

		PrivateIncludePathModuleNames.Add("DesktopPlatform");
	}
}
