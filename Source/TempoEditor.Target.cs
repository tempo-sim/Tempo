// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class TempoEditorTarget : TargetRules
{
	public TempoEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("Tempo");

		if (Platform == UnrealTargetPlatform.Win64)
		{
			ToolChainName = "TempoVCToolChain";
		}
		else if (Platform == UnrealTargetPlatform.Mac)
		{
			ToolChainName = "TempoMacToolChain";
		}
		else if (Platform == UnrealTargetPlatform.Linux)
		{
			ToolChainName = "TempoLinuxToolChain";
		}
	}
}
