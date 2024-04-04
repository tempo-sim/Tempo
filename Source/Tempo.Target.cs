// Copyright Tempo Simulation, LLC. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class TempoTarget : TargetRules
{
	public TempoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("Tempo");
	}
}
