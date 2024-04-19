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
		ToolChainName = "TempoVCToolChain";
	}
}
