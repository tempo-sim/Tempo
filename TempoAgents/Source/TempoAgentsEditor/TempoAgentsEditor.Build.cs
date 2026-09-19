// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System.Diagnostics;
using System.IO;
using UnrealBuildTool;

public class TempoAgentsEditor : TempoModuleRules
{
	public TempoAgentsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
			);


		PrivateIncludePaths.AddRange(
			new string[]
			{
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// Unreal
				"CoreUObject",
				"DeveloperSettings",
				"EditorFramework",
				"InputCore",
				"Projects",
				"UnrealEd",
				"MassEntity",
				"MassTraffic",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"ZoneGraph",
				// Tempo
				"TempoAgents",
				"TempoCore",
			}
			);

		// UE 5.8 split the core Mass types out of the MassEntity plugin into a new MassCore module.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8)
		{
			PrivateDependencyModuleNames.Add("MassCore");
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);

		GenerateEngineDerivedSources();
	}

	// Part of this module's source is generated from the engine's (see EngineDerived/README.md).
	// The plugin's pre-build step is what keeps it up to date and reports any failure. But UBT skips
	// a pre-build step in the build that first learns of it, and these rules always run before UBT
	// looks for this module's source files, so the sources are also generated here.
	private void GenerateEngineDerivedSources()
	{
		string PythonRelativePath;
		if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
		{
			PythonRelativePath = Path.Combine("Win64", "python.exe");
		}
		else if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
		{
			PythonRelativePath = Path.Combine("Mac", "bin", "python3");
		}
		else
		{
			PythonRelativePath = Path.Combine("Linux", "bin", "python3");
		}

		string PluginDirectory = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		ProcessStartInfo StartInfo = new ProcessStartInfo
		{
			FileName = Path.Combine(EngineDirectory, "Binaries", "ThirdParty", "Python3", PythonRelativePath),
			UseShellExecute = false,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
		};
		StartInfo.ArgumentList.Add(Path.Combine(PluginDirectory, "Content", "Python", "gen_engine_derived.py"));
		StartInfo.ArgumentList.Add(EngineDirectory);
		StartInfo.ArgumentList.Add(PluginDirectory);

		try
		{
			using (Process GenerateProcess = Process.Start(StartInfo))
			{
				GenerateProcess.StandardOutput.ReadToEnd();
				GenerateProcess.StandardError.ReadToEnd();
				GenerateProcess.WaitForExit();
			}
		}
		catch (System.Exception)
		{
		}
	}
}
